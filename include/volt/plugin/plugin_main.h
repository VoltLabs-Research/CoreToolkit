#pragma once

#include <volt/cli/common.h>
#include <volt/core/analysis_result.h>
#include <volt/core/lammps_parser.h>
#include <oneapi/tbb/global_control.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace Volt::Plugin {

using json = nlohmann::json;
using OptsMap = std::map<std::string, std::string>;

struct CliOption {
    std::string name;
    std::string type;
    std::string help;
    std::string defaultVal;
};

struct PluginDescriptor {
    std::string name;
    std::string description;
    std::vector<CliOption> options;
    bool needsReferenceFrame = false;
};

using PluginRunFn = std::function<json(
    const std::map<std::string, std::string>& opts,
    const LammpsParser::Frame& frame,
    const LammpsParser::Frame* refFrame,
    const std::string& outputBase
)>;

inline void showPluginUsage(const std::string& argv0, const PluginDescriptor& desc) {
    CLI::printUsageHeader(argv0, "Volt - " + desc.description);
    for (const auto& opt : desc.options) {
        std::cerr << "  " << opt.name << " <" << opt.type << ">";
        const std::size_t pad = (opt.name.size() + opt.type.size() + 4 < 32)
            ? (32 - opt.name.size() - opt.type.size() - 4) : 2;
        std::cerr << std::string(pad, ' ') << opt.help;
        if (!opt.defaultVal.empty())
            std::cerr << " [default: " << opt.defaultVal << "]";
        std::cerr << "\n";
    }
    if (desc.needsReferenceFrame) {
        std::cerr << "  --reference <file>            Reference LAMMPS dump file.\n";
    }
    std::cerr << "  --threads <int>               Max worker threads. [default: auto]\n";
    CLI::printHelpOption();
}

inline int pluginMain(int argc, char* argv[], const PluginDescriptor& desc, PluginRunFn run) {
    if (argc < 2) {
        showPluginUsage(argv[0], desc);
        return 1;
    }

    std::string filename, outputBase;
    auto opts = CLI::parseArgs(argc, argv, filename, outputBase);

    if (CLI::hasOption(opts, "--help") || filename.empty()) {
        showPluginUsage(argv[0], desc);
        return filename.empty() ? 1 : 0;
    }

    const int requestedThreads = std::max(1, CLI::getInt(opts, "--threads",
        std::thread::hardware_concurrency() > 0
            ? static_cast<int>(std::thread::hardware_concurrency()) : 1));
    oneapi::tbb::global_control parallelControl(
        oneapi::tbb::global_control::max_allowed_parallelism,
        static_cast<std::size_t>(requestedThreads));

    CLI::initLogging(desc.name);
    spdlog::info("Using {} threads (OneTBB)", requestedThreads);

    LammpsParser::Frame frame;
    if (!CLI::parseFrame(filename, frame)) return 1;

    LammpsParser::Frame refFrame;
    const LammpsParser::Frame* refFramePtr = nullptr;

    if (desc.needsReferenceFrame) {
        std::string refFile = CLI::getString(opts, "--reference");
        if (!refFile.empty()) {
            spdlog::info("Parsing reference file: {}", refFile);
            LammpsParser refParser;
            if (!refParser.parseFile(refFile, refFrame)) {
                spdlog::error("Failed to parse reference file: {}", refFile);
                return 1;
            }
            if (refFrame.natoms != frame.natoms) {
                spdlog::error("Atom count mismatch: current={} reference={}",
                    frame.natoms, refFrame.natoms);
                return 1;
            }
            refFramePtr = &refFrame;
            spdlog::info("Reference loaded: {} atoms", refFrame.natoms);
        }
    }

    outputBase = CLI::deriveOutputBase(filename, outputBase);
    spdlog::info("Output base: {}", outputBase);

    json result = run(opts, frame, refFramePtr, outputBase);

    if (result.value("is_failed", false)) {
        spdlog::error("Analysis failed: {}", result.value("error", "Unknown error"));
        return 1;
    }

    spdlog::info("{} completed.", desc.description);
    return 0;
}

} // namespace Volt::Plugin

#define VOLT_PLUGIN_MAIN(descriptor, ...) \
    int main(int argc, char* argv[]) { \
        return Volt::Plugin::pluginMain(argc, argv, descriptor, __VA_ARGS__); \
    }
