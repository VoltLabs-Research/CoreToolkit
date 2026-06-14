#pragma once

#include <volt/math/point3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

// Per-bond dynamic column writer, the bond-entity counterpart of
// ColumnarLineWriter / ColumnarAtomWriter. Plugins call field() once per
// property per bond; columns are created on first use and back-filled with
// nulls so every row stays aligned. DuckDB/Parquet lives entirely inside
// CoreToolkit — plugins never include DuckDB headers.
class ColumnarBondWriter {
public:
    void field(const std::string& name, double value);
    void field(const std::string& name, std::int64_t value);
    void field(const std::string& name, int value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, std::size_t value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, bool value) { field(name, static_cast<std::int64_t>(value ? 1 : 0)); }
    void field(const std::string& name, const std::string& value);
    void field(const std::string& name, const char* value) { field(name, std::string(value)); }
    // Fixed-width vector (e.g. an RGB color) stored as a Parquet list<double> column.
    void field(const std::string& name, const std::vector<double>& values);

    struct Impl;
    explicit ColumnarBondWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

// One bond between two atoms of a frame. `posA`/`posB` are the absolute
// rendered endpoints: posB already carries the periodic-image shift the bond
// crosses, so the geometry is continuous across the cell. `pbcShift` records
// that integer image offset (per-axis) for downstream PBC-aware consumers.
struct Bond {
    std::int64_t id = 0;
    std::int32_t atomA = 0;
    std::int32_t atomB = 0;
    std::array<std::int32_t, 3> pbcShift{ {0, 0, 0} };
    double distance = 0.0;
    Point3 posA{ Point3::Origin() };
    Point3 posB{ Point3::Origin() };
};

// Per-bond dynamic columns supplied by the plugin (bond_order, strength,
// type, color, ...).
using PerBondColumnWriter = std::function<void(ColumnarBondWriter& writer, std::size_t bondIndex)>;

// Streams a bond-entity Parquet table (ZSTD) for a frame. Fixed columns:
//   id (uint64), points (list<list<double>>, the two endpoints [x, y, z]),
//   atom_a (int32), atom_b (int32), pbc_shift_x/y/z (int32), distance (double)
// plus any dynamic per-bond property columns.
//
// The `points` column carries the two rendered endpoints inline (posA, posB),
// so the bond table is self-contained the same way the line table is: the GLB
// export reads `points` (a bond is a two-vertex polyline → the line tube path),
// styling/filtering query the property columns by id, and listings derive from
// the property columns. No join against atoms.parquet is required — atom_a /
// atom_b stay as references for analyses that want them.
void streamBondsToParquet(
    const std::string& filePath,
    const std::vector<Bond>& bonds,
    const PerBondColumnWriter& writePerBondColumns = {}
);

}
