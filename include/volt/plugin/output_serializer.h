#pragma once

#include <volt/utilities/json_utils.h>
#include <volt/utilities/parquet_atom_writer.h>
#include <volt/core/lammps_parser.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace Volt::Plugin {

using json = nlohmann::json;

struct OutputConfig {
    std::string summaryFileSuffix;
    BucketResolver bucketResolver;
    PerAtomColumnWriter perAtomColumnWriter;
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
        const std::string outputPath = outputBase + config.summaryFileSuffix + ".parquet";
        if (JsonUtils::writeJsonToParquet(result, outputPath)) {
            spdlog::info("Summary parquet written to {}", outputPath);
        } else {
            spdlog::warn("Could not write summary parquet: {}", outputPath);
        }
    }

    if (config.bucketResolver) {
        const std::string atomsPath = outputBase + "_atoms.parquet";
        streamAtomsToParquet(atomsPath, frame, config.bucketResolver, config.perAtomColumnWriter,
                             config.resolveStructureId, config.includeStructureColumns);
        spdlog::info("Exported atoms data to: {}", atomsPath);
    }
}

}
