#pragma once

#include <volt/math/point3.h>

#include <array>
#include <string>
#include <vector>

namespace Volt {

void streamMeshTablesToParquet(
    const std::string& verticesPath,
    const std::string& facetsPath,
    const std::vector<Point3>& vertices,
    const std::vector<std::array<int, 3>>& facets
);

}
