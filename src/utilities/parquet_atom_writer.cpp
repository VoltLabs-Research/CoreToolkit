#include <volt/utilities/parquet_atom_writer.h>
#include <volt/utilities/duckdb_parquet.h>
#include <volt/utilities/parquet_writer_log.h>
#include <volt/core/progress.h>
#include <volt/math/point3.h>
#include <volt/plugin/output_files.h>

#include <duckdb.hpp>

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace Volt {

void streamAtomsToParquet(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const RowColumnWriter& writePerAtomColumns,
    const StructureIdResolver& resolveStructureId,
    bool includeStructureColumns
){
    ParquetWriterLog log;
    const std::size_t natoms = static_cast<std::size_t>(frame.natoms);

    std::map<std::string, std::int32_t> bucketId;
    auto idForBucket = [&](const std::string& name) -> std::int32_t {
        auto it = bucketId.find(name);
        if(it != bucketId.end()) return it->second;
        const std::int32_t id = static_cast<std::int32_t>(bucketId.size());
        bucketId.emplace(name, id);
        return id;
    };

    std::vector<std::uint32_t> atomIndex; atomIndex.reserve(natoms);
    std::vector<std::uint64_t> ids;        ids.reserve(natoms);
    std::vector<double> xs, ys, zs;        xs.reserve(natoms); ys.reserve(natoms); zs.reserve(natoms);
    std::vector<std::string> buckets;      buckets.reserve(natoms);
    std::vector<std::int32_t> structureIds; structureIds.reserve(natoms);

    ColumnarRowWriter::Impl dyn({"atom_index", "id", "x", "y", "z", "bucket", "structure_id", "structure_name"});
    ColumnarRowWriter writer(dyn);

    ProgressSteps steps({
        {"Collecting atom rows", 1.0},
        {"Loading atom rows into DuckDB", 1.0},
        {"Writing " + std::to_string(natoms) + " atom rows to Parquet", 1.0},
    });
    ProgressReporter collectProgress("  Collecting atom rows", natoms);
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
        collectProgress.update(i + 1);
    }
    log.dump(writer);
    steps.next();

    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);

    std::string ddl =
        "CREATE TABLE atoms("
        "atom_index UINTEGER, id UBIGINT, x DOUBLE, y DOUBLE, z DOUBLE, "
        "bucket VARCHAR";
    if(includeStructureColumns)
        ddl += ", structure_id INTEGER, structure_name VARCHAR";
    ddl += dyn.columnsDdl();
    ddl += ')';
    Detail::createTable(con, ddl, "atoms", filePath);

    {
        duckdb::Appender appender(con, "atoms");
        ProgressReporter loadProgress("  Loading atom rows into DuckDB", natoms);
        for(std::size_t i = 0; i < natoms; ++i){
            appender.BeginRow();
            appender.Append<std::uint32_t>(atomIndex[i]);
            appender.Append<std::uint64_t>(ids[i]);
            appender.Append<double>(xs[i]);
            appender.Append<double>(ys[i]);
            appender.Append<double>(zs[i]);
            appender.Append(duckdb::Value(buckets[i]));
            if(includeStructureColumns){
                appender.Append<std::int32_t>(structureIds[i]);
                appender.Append(duckdb::Value(buckets[i]));
            }
            dyn.appendColumns(appender, i);
            appender.EndRow();
            loadProgress.update(i + 1);
        }
        appender.Close();
        log.dump(appender);
    }
    steps.next();

    Detail::copyTableToParquet(con, "atoms", filePath);

    log.dump(ParquetAtomRows{natoms, 6 + (includeStructureColumns ? 2 : 0) + dyn.columns.size(), filePath});
}

AtomsTable writeAtomsTable(
    const std::string& outputBase,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const RowColumnWriter& writePerAtomColumns,
    const StructureIdResolver& resolveStructureId,
    bool includeStructureColumns
){
    AtomsTable table{outputPath(outputBase, "atoms")};
    streamAtomsToParquet(table.path, frame, resolveBucket, writePerAtomColumns, resolveStructureId, includeStructureColumns);
    return table;
}

}
