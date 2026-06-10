#pragma once

#include <volt/core/lammps_parser.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

// Assigns a bucket name to each atom (e.g. "All", "VALID", "Coordination_6").
// The bucket becomes a column; listings are GROUP BY bucket over the table.
using BucketResolver = std::function<std::string(std::size_t atomIndex)>;

// Per-atom dynamic column writer. Plugins call field() once per property per
// atom; columns are created on first use and back-filled with nulls so every
// row stays aligned. Arrow/Parquet lives entirely inside CoreToolkit — plugins
// never include Arrow headers.
class ColumnarAtomWriter {
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
    explicit ColumnarAtomWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

// Per-atom dynamic columns supplied by the plugin (coordination, csp, cluster_id...).
using PerAtomColumnWriter = std::function<void(ColumnarAtomWriter& writer, std::size_t atomIndex)>;

// Optional resolver for the `structure_id` column. When omitted, structure_id is
// the first-seen ordinal of the atom's bucket. Plugins with a meaningful code
// (e.g. crystal StructureType) provide it explicitly.
using StructureIdResolver = std::function<int(std::size_t atomIndex)>;

// Streams a per-atom Parquet table (ZSTD) for a frame. Fixed columns:
//   atom_index (uint32), id (uint64), x/y/z (double), bucket (string),
//   structure_id (int32), structure_name (string), cluster_id (int32)
// plus any dynamic columns emitted via writePerAtomColumns.
//
// This single table serves all per-atom consumers: coloring/filtering query the
// property columns, the GLB export reads x/y/z + bucket, and listings/sub-listings
// are GROUP BY bucket / WHERE bucket = X.
void streamAtomsToParquet(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const PerAtomColumnWriter& writePerAtomColumns = {},
    const StructureIdResolver& resolveStructureId = {}
);

}
