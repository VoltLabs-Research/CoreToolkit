#include <volt/utilities/parquet_mesh_writer.h>
#include <volt/utilities/duckdb_parquet.h>
#include <volt/utilities/parquet_writer_log.h>

#include <duckdb.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Volt {

void streamMeshTablesToParquet(
    const std::string& verticesPath,
    const std::string& facetsPath,
    const std::vector<Point3>& vertices,
    const std::vector<std::array<int, 3>>& facets
){
    ParquetWriterLog log;
    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);

    Detail::createTable(con,
        "CREATE TABLE vertices(slot BIGINT, vertex_id BIGINT, x DOUBLE, y DOUBLE, z DOUBLE)",
        "vertices", verticesPath);
    Detail::createTable(con,
        "CREATE TABLE facets(ord BIGINT, a BIGINT, b BIGINT, c BIGINT)",
        "facets", facetsPath);

    {
        duckdb::Appender appender(con, "vertices");
        for(std::size_t i = 0; i < vertices.size(); ++i){
            const auto slot = static_cast<std::int64_t>(i);
            appender.BeginRow();
            appender.Append<std::int64_t>(slot);
            appender.Append<std::int64_t>(slot);
            appender.Append<double>(vertices[i].x());
            appender.Append<double>(vertices[i].y());
            appender.Append<double>(vertices[i].z());
            appender.EndRow();
        }
        appender.Close();
    }

    {
        duckdb::Appender appender(con, "facets");
        for(std::size_t i = 0; i < facets.size(); ++i){
            appender.BeginRow();
            appender.Append<std::int64_t>(static_cast<std::int64_t>(i));
            appender.Append<std::int64_t>(static_cast<std::int64_t>(facets[i][0]));
            appender.Append<std::int64_t>(static_cast<std::int64_t>(facets[i][1]));
            appender.Append<std::int64_t>(static_cast<std::int64_t>(facets[i][2]));
            appender.EndRow();
        }
        appender.Close();
    }

    Detail::copyTableToParquet(con, "vertices", verticesPath);
    Detail::copyTableToParquet(con, "facets", facetsPath);

    log.dump(ParquetMeshTables{vertices.size(), verticesPath, facets.size(), facetsPath});
}

nlohmann::json writeMeshTables(
    const std::string& filePath,
    const std::vector<Point3>& vertices,
    const std::vector<std::array<int, 3>>& facets,
    const SimulationCell& cell
){
    constexpr std::string_view extension = ".parquet";
    const std::string basePath = filePath.ends_with(extension)
        ? filePath.substr(0, filePath.size() - extension.size())
        : filePath;
    const std::string verticesPath = basePath + ".vertices.parquet";
    const std::string facetsPath = basePath + ".facets.parquet";
    streamMeshTablesToParquet(verticesPath, facetsPath, vertices, facets);

    const AffineTransformation& matrix = cell.matrix();
    nlohmann::json doc;
    doc["main_listing"] = {
        {"total_nodes", vertices.size()},
        {"total_facets", facets.size()}
    };
    doc["export"]["MeshExporter"][kParquetSourceKey] = {
        {"vertices", std::filesystem::path(verticesPath).filename().string()},
        {"facets", std::filesystem::path(facetsPath).filename().string()},
        {"cell", {
            {"matrix", {matrix.column(0), matrix.column(1), matrix.column(2)}},
            {"origin", matrix.column(3)},
            {"pbc", cell.pbcFlags()}
        }}
    };
    return doc;
}

}
