#pragma once

#include <volt/math/point3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

class ColumnarBondWriter {
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
    explicit ColumnarBondWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

struct Bond {
    std::int64_t id = 0;
    std::int32_t atomA = 0;
    std::int32_t atomB = 0;
    std::array<std::int32_t, 3> pbcShift{ {0, 0, 0} };
    double distance = 0.0;
    Point3 posA{ Point3::Origin() };
    Point3 posB{ Point3::Origin() };
};

using PerBondColumnWriter = std::function<void(ColumnarBondWriter& writer, std::size_t bondIndex)>;

void streamBondsToParquet(
    const std::string& filePath,
    const std::vector<Bond>& bonds,
    const PerBondColumnWriter& writePerBondColumns = {}
);

}
