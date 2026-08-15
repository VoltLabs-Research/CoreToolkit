#pragma once

#include <volt/math/point3.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

class ColumnarLineWriter {
public:
    void field(const std::string& name, double value);
    void field(const std::string& name, std::int64_t value);
    void field(const std::string& name, int value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, std::size_t value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, bool value) { field(name, static_cast<std::int64_t>(value ? 1 : 0)); }
    void field(const std::string& name, const std::string& value);
    void field(const std::string& name, const char* value) { field(name, std::string(value)); }
    void field(const std::string& name, const std::vector<double>& values);

    struct Impl;
    explicit ColumnarLineWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

using LinePointsResolver = std::function<void(std::size_t lineIndex, std::vector<Point3>& outPoints)>;

using PerLineColumnWriter = std::function<void(ColumnarLineWriter& writer, std::size_t lineIndex)>;

void streamLinesToParquet(
    const std::string& filePath,
    std::size_t lineCount,
    const LinePointsResolver& resolvePoints,
    const PerLineColumnWriter& writePerLineColumns = {}
);

}
