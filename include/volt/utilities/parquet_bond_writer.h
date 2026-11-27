#pragma once

#include <volt/math/point3.h>
#include <volt/utilities/columnar_row_writer.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Volt {

struct Bond {
    std::int64_t id = 0;
    std::int32_t atomA = 0;
    std::int32_t atomB = 0;
    std::array<std::int32_t, 3> pbcShift{ {0, 0, 0} };
    double distance = 0.0;
    Point3 posA{ Point3::Origin() };
    Point3 posB{ Point3::Origin() };
};

void streamBondsToParquet(
    const std::string& filePath,
    const std::vector<Bond>& bonds,
    const RowColumnWriter& writePerBondColumns = {}
);

}
