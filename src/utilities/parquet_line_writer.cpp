#include <volt/utilities/parquet_line_writer.h>
#include <volt/utilities/duckdb_parquet.h>
#include <volt/utilities/parquet_writer_log.h>

#include <duckdb.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace Volt {

void streamLinesToParquet(
    const std::string& filePath,
    std::size_t lineCount,
    const LinePointsResolver& resolvePoints,
    const RowColumnWriter& writePerLineColumns
){
    ParquetWriterLog log;
    const duckdb::LogicalType pointType = duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE);

    std::vector<duckdb::Value> pointLists;
    pointLists.reserve(lineCount);

    ColumnarRowWriter::Impl dyn({"id", "points"});
    ColumnarRowWriter writer(dyn);

    std::vector<Point3> linePoints;
    std::size_t pointCount = 0;
    for(std::size_t i = 0; i < lineCount; ++i){
        linePoints.clear();
        if(resolvePoints) resolvePoints(i, linePoints);
        pointCount += linePoints.size();

        std::vector<duckdb::Value> points;
        points.reserve(linePoints.size());
        for(const Point3& p : linePoints){
            points.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, {
                duckdb::Value::DOUBLE(p.x()),
                duckdb::Value::DOUBLE(p.y()),
                duckdb::Value::DOUBLE(p.z())
            }));
        }
        pointLists.emplace_back(duckdb::Value::LIST(pointType, std::move(points)));

        if(writePerLineColumns) writePerLineColumns(writer, i);
        dyn.finishRow();
    }

    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);

    std::string ddl = "CREATE TABLE lines(id UBIGINT, points DOUBLE[][]";
    ddl += dyn.columnsDdl();
    ddl += ')';
    Detail::createTable(con, ddl, "lines", filePath);

    {
        duckdb::Appender appender(con, "lines");
        for(std::size_t i = 0; i < lineCount; ++i){
            appender.BeginRow();
            appender.Append<std::uint64_t>(static_cast<std::uint64_t>(i));
            appender.Append(pointLists[i]);
            dyn.appendColumns(appender, i);
            appender.EndRow();
        }
        appender.Close();
    }

    Detail::copyTableToParquet(con, "lines", filePath);

    log.dump(ParquetLineRows{lineCount, 2 + dyn.columns.size(), pointCount, filePath});
}

}
