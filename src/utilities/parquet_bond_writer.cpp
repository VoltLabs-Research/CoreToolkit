#include <volt/utilities/parquet_bond_writer.h>
#include <volt/utilities/duckdb_parquet.h>
#include <volt/utilities/parquet_writer_log.h>

#include <duckdb.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace Volt {

void streamBondsToParquet(
    const std::string& filePath,
    const std::vector<Bond>& bonds,
    const RowColumnWriter& writePerBondColumns
){
    ParquetWriterLog log;
    const std::size_t bondCount = bonds.size();
    const duckdb::LogicalType pointType = duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE);

    std::vector<duckdb::Value> pointLists;
    pointLists.reserve(bondCount);

    ColumnarRowWriter::Impl dyn({"id", "points", "atom_a", "atom_b", "pbc_shift_x", "pbc_shift_y", "pbc_shift_z", "distance"});
    ColumnarRowWriter writer(dyn);

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

    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);

    std::string ddl =
        "CREATE TABLE bonds("
        "id UBIGINT, points DOUBLE[][], atom_a INTEGER, atom_b INTEGER, "
        "pbc_shift_x INTEGER, pbc_shift_y INTEGER, pbc_shift_z INTEGER, distance DOUBLE";
    ddl += dyn.columnsDdl();
    ddl += ')';
    Detail::createTable(con, ddl, "bonds", filePath);

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
            dyn.appendColumns(appender, i);
            appender.EndRow();
        }
        appender.Close();
    }

    Detail::copyTableToParquet(con, "bonds", filePath);

    log.dump(ParquetBondRows{bondCount, 8 + dyn.columns.size(), filePath});
}

}
