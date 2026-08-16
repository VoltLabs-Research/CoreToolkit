#include <volt/utilities/parquet_bond_writer.h>
#include <volt/utilities/duckdb_parquet.h>

#include <duckdb.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Volt {

using ColType = Detail::ColType;
using DynColumn = Detail::DynColumn;

struct ColumnarBondWriter::Impl {
    std::vector<DynColumn> columns;
    std::unordered_map<std::string, std::size_t> index;
    std::int64_t rowsCompleted = 0;

    static bool isReserved(const std::string& name){
        return name == "id" || name == "points" || name == "atom_a" || name == "atom_b"
            || name == "pbc_shift_x" || name == "pbc_shift_y" || name == "pbc_shift_z"
            || name == "distance";
    }

    DynColumn& ensure(const std::string& name, ColType type){
        auto it = index.find(name);
        if(it != index.end()) return columns[it->second];

        DynColumn col;
        col.name = name;
        col.type = type;
        const duckdb::LogicalType lt = Detail::logicalTypeFor(type);
        col.values.reserve(static_cast<std::size_t>(rowsCompleted) + 1);
        for(std::int64_t r = 0; r < rowsCompleted; ++r) col.values.emplace_back(lt);

        index.emplace(name, columns.size());
        columns.push_back(std::move(col));
        return columns.back();
    }

    void appendDouble(const std::string& name, double v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Double);
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
            if(!c.touchedThisRow) c.values.emplace_back(Detail::logicalTypeFor(c.type));
            c.touchedThisRow = false;
        }
        ++rowsCompleted;
    }
};

void ColumnarBondWriter::field(const std::string& name, double value){ _impl.appendDouble(name, value); }
void ColumnarBondWriter::field(const std::string& name, std::int64_t value){ _impl.appendInt64(name, value); }
void ColumnarBondWriter::field(const std::string& name, const std::string& value){ _impl.appendString(name, value); }
void ColumnarBondWriter::field(const std::string& name, const std::vector<double>& values){ _impl.appendList(name, values); }

void streamBondsToParquet(
    const std::string& filePath,
    const std::vector<Bond>& bonds,
    const PerBondColumnWriter& writePerBondColumns
){
    const std::size_t bondCount = bonds.size();
    const duckdb::LogicalType pointType = duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE);

    std::vector<duckdb::Value> pointLists;
    pointLists.reserve(bondCount);

    ColumnarBondWriter::Impl dyn;
    ColumnarBondWriter writer(dyn);

    for(std::size_t i = 0; i < bondCount; ++i){
        const Bond& bond = bonds[i];

        std::vector<duckdb::Value> endpoints;
        endpoints.reserve(2);
        endpoints.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, {
            duckdb::Value::DOUBLE(bond.posA.x()),
            duckdb::Value::DOUBLE(bond.posA.y()),
            duckdb::Value::DOUBLE(bond.posA.z())
        }));
        endpoints.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, {
            duckdb::Value::DOUBLE(bond.posB.x()),
            duckdb::Value::DOUBLE(bond.posB.y()),
            duckdb::Value::DOUBLE(bond.posB.z())
        }));
        pointLists.emplace_back(duckdb::Value::LIST(pointType, std::move(endpoints)));

        if(writePerBondColumns) writePerBondColumns(writer, i);
        dyn.finishRow();
    }

    try {
        auto db = Volt::Detail::openInMemoryDb();
        duckdb::Connection con(*db);

        std::string ddl =
            "CREATE TABLE bonds("
            "id UBIGINT, points DOUBLE[][], atom_a INTEGER, atom_b INTEGER, "
            "pbc_shift_x INTEGER, pbc_shift_y INTEGER, pbc_shift_z INTEGER, distance DOUBLE";
        for(const auto& col : dyn.columns){
            ddl += ", ";
            ddl += Detail::quoteIdent(col.name);
            ddl += ' ';
            ddl += Detail::sqlTypeFor(col.type);
        }
        ddl += ')';
        if(con.Query(ddl)->HasError()) return;

        {
            duckdb::Appender appender(con, "bonds");
            for(std::size_t i = 0; i < bondCount; ++i){
                const Bond& bond = bonds[i];
                appender.BeginRow();
                appender.Append<std::uint64_t>(static_cast<std::uint64_t>(bond.id));
                appender.Append(pointLists[i]);
                appender.Append<std::int32_t>(bond.atomA);
                appender.Append<std::int32_t>(bond.atomB);
                appender.Append<std::int32_t>(bond.pbcShift[0]);
                appender.Append<std::int32_t>(bond.pbcShift[1]);
                appender.Append<std::int32_t>(bond.pbcShift[2]);
                appender.Append<double>(bond.distance);
                for(auto& col : dyn.columns){
                    appender.Append(col.values[i]);
                }
                appender.EndRow();
            }
            appender.Close();
        }

        (void)Detail::copyTableToParquet(con, "bonds", filePath);
    } catch (...) {
    }
}

}
