#pragma once

#include <volt/utilities/json_utils.h>
#include <volt/utilities/msgpack_atom_writer.h>
#include <volt/core/lammps_parser.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>
#include <functional>

namespace Volt::Plugin {

using json = nlohmann::json;

struct OutputConfig {
    std::string summaryFileSuffix;
    BucketResolver bucketResolver;
    AtomExtraFieldWriter atomFieldWriter;
    PerAtomPropertyWriter perAtomFieldWriter;
};

inline void serializePluginOutput(
    const std::string& outputBase,
    const LammpsParser::Frame& frame,
    const json& result,
    const OutputConfig& config
) {
    if (!config.summaryFileSuffix.empty()) {
        const std::string outputPath = outputBase + config.summaryFileSuffix + ".msgpack";
        if (JsonUtils::writeJsonMsgpackToFile(result, outputPath, false)) {
            spdlog::info("Summary msgpack written to {}", outputPath);
        } else {
            spdlog::warn("Could not write summary msgpack: {}", outputPath);
        }
    }

    if (config.bucketResolver) {
        const std::string atomsPath = outputBase + "_atoms.msgpack";
        streamAtomsToFile(atomsPath, frame, config.bucketResolver, config.atomFieldWriter, config.perAtomFieldWriter);
        spdlog::info("Exported atoms data to: {}", atomsPath);
    }
}

} // namespace Volt::Plugin
