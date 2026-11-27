#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

// The columns a caller adds to a Parquet table, one row at a time. A column takes the type of the first value written
// to it, and a row that does not write it gets a null there.
class ColumnarRowWriter {
public:
    void field(const std::string& name, double value);
    void field(const std::string& name, std::int64_t value);
    void field(const std::string& name, int value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, std::size_t value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, bool value) { field(name, static_cast<std::int64_t>(value ? 1 : 0)); }
    void field(const std::string& name, const std::string& value);
    void field(const std::string& name, const char* value) { field(name, std::string(value)); }
    void field(const std::string& name, const std::vector<double>& values);
    // A Point3, Vector3 or Quaternion, as the list of its components.
    template<std::size_t N>
    void field(const std::string& name, const std::array<double, N>& components) {
        field(name, std::vector<double>(components.begin(), components.end()));
    }

    struct Impl;
    explicit ColumnarRowWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

// Writes the caller's columns of one row of a table.
using RowColumnWriter = std::function<void(ColumnarRowWriter& writer, std::size_t row)>;

}
