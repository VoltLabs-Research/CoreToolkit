#pragma once

#include <volt/math/point3.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

// Per-line dynamic column writer, the line-entity counterpart of
// ColumnarAtomWriter. Plugins call field() once per property per line; columns
// are created on first use and back-filled with nulls so every row stays
// aligned. DuckDB/Parquet lives entirely inside CoreToolkit — plugins never
// include DuckDB headers.
class ColumnarLineWriter {
public:
    void field(const std::string& name, double value);
    void field(const std::string& name, std::int64_t value);
    void field(const std::string& name, int value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, std::size_t value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, bool value) { field(name, static_cast<std::int64_t>(value ? 1 : 0)); }
    void field(const std::string& name, const std::string& value);
    void field(const std::string& name, const char* value) { field(name, std::string(value)); }
    // Fixed-width vector (e.g. a Burgers vector) stored as a Parquet list<double> column.
    void field(const std::string& name, const std::vector<double>& values);

    struct Impl;
    explicit ColumnarLineWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

// Supplies the polyline vertices of one line entity.
using LinePointsResolver = std::function<void(std::size_t lineIndex, std::vector<Point3>& outPoints)>;

// Per-line dynamic columns supplied by the plugin (burgers_family, length, ...).
using PerLineColumnWriter = std::function<void(ColumnarLineWriter& writer, std::size_t lineIndex)>;

// Streams a line-entity Parquet table (ZSTD) for a frame. Fixed columns:
//   id (uint64), points (list<list<double>>, each inner list [x, y, z])
// plus any dynamic per-line property columns.
//
// This single table serves all line-entity consumers, mirroring the per-atom
// table: the GLB export reads points, styling/filtering query the property
// columns by id, and listings derive from the property columns.
void streamLinesToParquet(
    const std::string& filePath,
    std::size_t lineCount,
    const LinePointsResolver& resolvePoints,
    const PerLineColumnWriter& writePerLineColumns = {}
);

}
