#pragma once

#include <volt/core/simulation_cell.h>
#include <volt/math/point3.h>

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <vector>

namespace Volt {

// The key under which an exporter's document names the Parquet tables that hold its data.
inline constexpr const char* kParquetSourceKey = "__parquet_source__";

void streamMeshTablesToParquet(
    const std::string& verticesPath,
    const std::string& facetsPath,
    const std::vector<Point3>& vertices,
    const std::vector<std::array<int, 3>>& facets
);

// Writes the vertex and facet tables next to filePath, as <base>.vertices.parquet and <base>.facets.parquet, and
// returns the document that points VOLT's MeshExporter to them, with their row counts as its main listing.
nlohmann::json writeMeshTables(
    const std::string& filePath,
    const std::vector<Point3>& vertices,
    const std::vector<std::array<int, 3>>& facets,
    const SimulationCell& cell
);

}
