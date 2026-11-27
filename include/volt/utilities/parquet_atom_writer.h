#pragma once

#include <volt/core/lammps_parser.h>
#include <volt/utilities/columnar_row_writer.h>
#include <volt/utilities/written_table.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

using BucketResolver = std::function<std::string(std::size_t atomIndex)>;

using StructureIdResolver = std::function<int(std::size_t atomIndex)>;

void streamAtomsToParquet(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const RowColumnWriter& writePerAtomColumns = {},
    const StructureIdResolver& resolveStructureId = {},
    bool includeStructureColumns = false
);

// Writes the per-atom table of a run next to the output base, as <base>_atoms.parquet.
AtomsTable writeAtomsTable(
    const std::string& outputBase,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const RowColumnWriter& writePerAtomColumns = {},
    const StructureIdResolver& resolveStructureId = {},
    bool includeStructureColumns = false
);

}
