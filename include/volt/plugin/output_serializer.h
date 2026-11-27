#pragma once

#include <volt/utilities/json_utils.h>
#include <volt/utilities/parquet_atom_writer.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/run_log.h>
#include <nlohmann/json.hpp>

#include <string>

namespace Volt::Plugin {

using json = nlohmann::json;

struct OutputConfig {
    std::string summaryFileSuffix;
    BucketResolver bucketResolver;
    RowColumnWriter perAtomColumnWriter;
    StructureIdResolver resolveStructureId;
    bool includeStructureColumns = false;
};

inline void serializePluginOutput(
    const std::string& outputBase,
    const LammpsParser::Frame& frame,
    const json& result,
    const OutputConfig& config
) {
    if (!config.summaryFileSuffix.empty()) {
        // The suffix carries the separator of the table's name ("_ackland_jones").
        RunLog().dump(JsonUtils::writeSummaryTable(result, outputBase, config.summaryFileSuffix.substr(1)));
    }

    if (config.bucketResolver) {
        writeAtomsTable(outputBase, frame, config.bucketResolver, config.perAtomColumnWriter,
                        config.resolveStructureId, config.includeStructureColumns);
    }
}

}
