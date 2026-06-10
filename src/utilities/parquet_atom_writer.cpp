#include <volt/utilities/parquet_atom_writer.h>
#include <volt/utilities/duckdb_parquet.h>
#include <volt/math/point3.h>

#include <duckdb.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Volt {

namespace {

enum class ColType { Double, Int64, String, ListDouble };

// A dynamic column buffers one DuckDB Value per completed row. Columns are created
// on first use and back-filled with typed NULLs so every row stays aligned; the
// full schema is only known after the atom loop, so the table is materialised and
// written at the end (mirrors the previous Arrow buffer-then-write design).
struct DynColumn {
    std::string name;
    ColType type;
    std::vector<duckdb::Value> values;
    bool touchedThisRow = false;
};

duckdb::LogicalType logicalTypeFor(ColType type){
    switch(type){
        case ColType::Double:     return duckdb::LogicalType::DOUBLE;
        case ColType::Int64:      return duckdb::LogicalType::BIGINT;
        case ColType::String:     return duckdb::LogicalType::VARCHAR;
        case ColType::ListDouble: return duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE);
    }
    return duckdb::LogicalType::DOUBLE;
}

const char* sqlTypeFor(ColType type){
    switch(type){
        case ColType::Double:     return "DOUBLE";
        case ColType::Int64:      return "BIGINT";
        case ColType::String:     return "VARCHAR";
        case ColType::ListDouble: return "DOUBLE[]";
    }
    return "DOUBLE";
}

// Quotes a SQL identifier (column name) with double quotes, doubling any embedded
// double quote. Plugin-supplied property names are untrusted as identifiers.
std::string quoteIdent(const std::string& name){
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back('"');
    for(char c : name){
        if(c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // namespace

struct ColumnarAtomWriter::Impl {
    std::vector<DynColumn> columns;
    std::unordered_map<std::string, std::size_t> index;
    std::int64_t rowsCompleted = 0;

    // Fixed columns owned by streamAtomsToParquet; a plugin must not shadow them.
    static bool isReserved(const std::string& name){
        return name == "atom_index" || name == "id" || name == "x" || name == "y"
            || name == "z" || name == "bucket" || name == "structure_id"
            || name == "structure_name";
    }

    DynColumn& ensure(const std::string& name, ColType type){
        auto it = index.find(name);
        if(it != index.end()) return columns[it->second];

        DynColumn col;
        col.name = name;
        col.type = type;
        // Back-fill typed NULLs for rows that completed before this column appeared.
        const duckdb::LogicalType lt = logicalTypeFor(type);
        col.values.reserve(static_cast<std::size_t>(rowsCompleted) + 1);
        for(std::int64_t r = 0; r < rowsCompleted; ++r) col.values.emplace_back(lt);

        index.emplace(name, columns.size());
        columns.push_back(std::move(col));
        return columns.back();
    }

    void appendDouble(const std::string& name, double v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Double);
        // Match the established column type (types are consistent per name in
        // practice; coerce defensively rather than mismatch the schema).
        if(c.type == ColType::Int64) c.values.emplace_back(duckdb::Value::BIGINT(static_cast<std::int64_t>(v)));
        else                          c.values.emplace_back(duckdb::Value::DOUBLE(v));
        c.touchedThisRow = true;
    }
    void appendInt64(const std::string& name, std::int64_t v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Int64);
        if(c.type == ColType::Double) c.values.emplace_back(duckdb::Value::DOUBLE(static_cast<double>(v)));
        else                           c.values.emplace_back(duckdb::Value::BIGINT(v));
        c.touchedThisRow = true;
    }
    void appendString(const std::string& name, const std::string& v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::String);
        c.values.emplace_back(duckdb::Value(v));
        c.touchedThisRow = true;
    }
    void appendList(const std::string& name, const std::vector<double>& v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::ListDouble);
        std::vector<duckdb::Value> items;
        items.reserve(v.size());
        for(double d : v) items.emplace_back(duckdb::Value::DOUBLE(d));
        c.values.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, std::move(items)));
        c.touchedThisRow = true;
    }

    void finishRow(){
        for(auto& c : columns){
            if(!c.touchedThisRow) c.values.emplace_back(logicalTypeFor(c.type)); // typed NULL
            c.touchedThisRow = false;
        }
        ++rowsCompleted;
    }
};

void ColumnarAtomWriter::field(const std::string& name, double value){ _impl.appendDouble(name, value); }
void ColumnarAtomWriter::field(const std::string& name, std::int64_t value){ _impl.appendInt64(name, value); }
void ColumnarAtomWriter::field(const std::string& name, const std::string& value){ _impl.appendString(name, value); }
void ColumnarAtomWriter::field(const std::string& name, const std::vector<double>& values){ _impl.appendList(name, values); }

void streamAtomsToParquet(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const PerAtomColumnWriter& writePerAtomColumns,
    const StructureIdResolver& resolveStructureId
){
    const std::size_t natoms = static_cast<std::size_t>(frame.natoms);

    // Stable structure_id per bucket, assigned in first-seen order.
    std::map<std::string, std::int32_t> bucketId;
    auto idForBucket = [&](const std::string& name) -> std::int32_t {
        auto it = bucketId.find(name);
        if(it != bucketId.end()) return it->second;
        const std::int32_t id = static_cast<std::int32_t>(bucketId.size());
        bucketId.emplace(name, id);
        return id;
    };

    // Fixed columns buffered as native vectors (structure_name mirrors bucket).
    std::vector<std::uint32_t> atomIndex; atomIndex.reserve(natoms);
    std::vector<std::uint64_t> ids;        ids.reserve(natoms);
    std::vector<double> xs, ys, zs;        xs.reserve(natoms); ys.reserve(natoms); zs.reserve(natoms);
    std::vector<std::string> buckets;      buckets.reserve(natoms);
    std::vector<std::int32_t> structureIds; structureIds.reserve(natoms);

    ColumnarAtomWriter::Impl dyn;
    ColumnarAtomWriter writer(dyn);

    for(std::size_t i = 0; i < natoms; ++i){
        const std::string bucket = resolveBucket(i);
        const std::int32_t sid = resolveStructureId
            ? static_cast<std::int32_t>(resolveStructureId(i))
            : idForBucket(bucket);

        atomIndex.push_back(static_cast<std::uint32_t>(i));
        ids.push_back(i < frame.ids.size()
            ? static_cast<std::uint64_t>(frame.ids[i])
            : static_cast<std::uint64_t>(i));
        const auto& pos = i < frame.positions.size() ? frame.positions[i] : Point3::Origin();
        xs.push_back(pos.x());
        ys.push_back(pos.y());
        zs.push_back(pos.z());
        buckets.push_back(bucket);
        structureIds.push_back(sid);

        if(writePerAtomColumns) writePerAtomColumns(writer, i);
        dyn.finishRow();
    }

    try {
        duckdb::DuckDB db(nullptr);
        duckdb::Connection con(db);

        // Build the table schema: fixed columns + dynamic columns in creation order.
        std::string ddl =
            "CREATE TABLE atoms("
            "atom_index UINTEGER, id UBIGINT, x DOUBLE, y DOUBLE, z DOUBLE, "
            "bucket VARCHAR, structure_id INTEGER, structure_name VARCHAR";
        for(const auto& col : dyn.columns){
            ddl += ", ";
            ddl += quoteIdent(col.name);
            ddl += ' ';
            ddl += sqlTypeFor(col.type);
        }
        ddl += ')';
        if(con.Query(ddl)->HasError()) return;

        {
            duckdb::Appender appender(con, "atoms");
            for(std::size_t i = 0; i < natoms; ++i){
                appender.BeginRow();
                appender.Append<std::uint32_t>(atomIndex[i]);
                appender.Append<std::uint64_t>(ids[i]);
                appender.Append<double>(xs[i]);
                appender.Append<double>(ys[i]);
                appender.Append<double>(zs[i]);
                appender.Append(duckdb::Value(buckets[i]));
                appender.Append<std::int32_t>(structureIds[i]);
                appender.Append(duckdb::Value(buckets[i])); // structure_name == bucket
                for(auto& col : dyn.columns){
                    appender.Append(col.values[i]);
                }
                appender.EndRow();
            }
            appender.Close();
        }

        (void)Detail::copyTableToParquet(con, "atoms", filePath);
    } catch (...) {
        // Writers return void / best-effort; swallow to preserve prior behaviour.
    }
}

}
