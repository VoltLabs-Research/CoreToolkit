#pragma once

#include <volt/math/point3.h>
#include <volt/utilities/columnar_row_writer.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

using LinePointsResolver = std::function<void(std::size_t lineIndex, std::vector<Point3>& outPoints)>;

void streamLinesToParquet(
    const std::string& filePath,
    std::size_t lineCount,
    const LinePointsResolver& resolvePoints,
    const RowColumnWriter& writePerLineColumns = {}
);

// One row per line: the points `points` gives for it (a member pointer or a function of the line), then the columns
// `columns` writes for it.
template<typename Line, typename Points, typename Columns>
void streamLinesToParquet(const std::string& filePath, const std::vector<Line>& lines, Points points, Columns columns){
    streamLinesToParquet(
        filePath,
        lines.size(),
        [&](std::size_t i, std::vector<Point3>& outPoints){ outPoints = std::invoke(points, lines[i]); },
        [&](ColumnarRowWriter& writer, std::size_t i){ columns(writer, lines[i]); }
    );
}

}
