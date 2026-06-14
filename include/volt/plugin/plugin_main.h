#pragma once

#include <volt/cli/common.h>
#include <volt/core/analysis_result.h>
#include <volt/core/lammps_parser.h>
#include <oneapi/tbb/global_control.h>

#include <functional>
#include <iostream>
#include <sstream>
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

// How many trajectory frames a plugin consumes per invocation. Single is the
// classic one-frame path; ReferencePair/Window/All are the multi-frame modes
// fed by the daemon's TrajectoryWindow node (workstream 15).
enum class FrameMode {
    Single,
    ReferencePair,
    Window,
    All
};

struct PluginDescriptor {
    std::string name;
    std::string description;
    std::vector<CliOption> options;
    // Single-frame reference-frame opt-in (legacy displacements/atomic-strain
    // path via --reference). Independent of frameMode below.
    bool needsReferenceFrame = false;
    // Multi-frame execution mode. When != Single the binary is launched through
    // pluginMainMultiFrame with the full window of frames.
    FrameMode frameMode = FrameMode::Single;
};

using PluginRunFn = std::function<json(
    const std::map<std::string, std::string>& opts,
    const LammpsParser::Frame& frame,
    const LammpsParser::Frame* refFrame,
    const std::string& outputBase
)>;

// Multi-frame run-fn: receives the localized window of parsed frames (in window
// order) plus the index of the primary frame (the one per-frame outputs are
// keyed to). `mode: 'all'` passes the whole trajectory; `window` a centered
// slice; `referencePair` exactly [reference, primary].
using PluginMultiFrameRunFn = std::function<json(
    const std::map<std::string, std::string>& opts,
    const std::vector<LammpsParser::Frame>& frames,
    std::size_t primaryIndex,
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
    if (desc.frameMode != FrameMode::Single) {
        std::cerr << "  --frames <f1,f2,...>          Comma/space-separated window dump files.\n";
        std::cerr << "  --primary <index>             Index of the primary frame in the window. [default: 0]\n";
    }
    std::cerr << "  --threads <int>               Max worker threads. [default: auto]\n";
    CLI::printHelpOption();
}

namespace Detail {

inline oneapi::tbb::global_control makeThreadControl(const OptsMap& opts) {
    const int requestedThreads = std::max(1, CLI::getInt(opts, "--threads",
        std::thread::hardware_concurrency() > 0
            ? static_cast<int>(std::thread::hardware_concurrency()) : 1));
    spdlog::info("Using {} threads (OneTBB)", requestedThreads);
    return oneapi::tbb::global_control(
        oneapi::tbb::global_control::max_allowed_parallelism,
        static_cast<std::size_t>(requestedThreads));
}

// Splits a `--frames` token list on commas and whitespace. The daemon emits the
// localized window paths space-joined via `{{ trajectory-window.framePaths }}`,
// but the CLI parser also collapses each into one option value, so accept both.
inline std::vector<std::string> splitFrameList(const std::string& raw) {
    std::vector<std::string> files;
    std::string current;
    for (char c : raw) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\n') {
            if (!current.empty()) {
                files.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) files.push_back(current);
    return files;
}

inline bool reportFailure(const json& result, const PluginDescriptor& desc) {
    if (result.value("is_failed", false)) {
        spdlog::error("Analysis failed: {}", result.value("error", "Unknown error"));
        return true;
    }
    spdlog::info("{} completed.", desc.description);
    return false;
}

} // namespace Detail

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

    auto parallelControl = Detail::makeThreadControl(opts);

    CLI::initLogging(desc.name);

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

    return Detail::reportFailure(result, desc) ? 1 : 0;
}

// Multi-frame entrypoint. The window file list arrives via `--frames`; the
// primary frame index via `--primary`. The first positional argument doubles as
// both a member of the window (so existing positional-path wiring keeps working)
// and the output-base anchor. STDOUT stays reserved for IPC; logs go to STDERR.
inline int pluginMainMultiFrame(int argc, char* argv[], const PluginDescriptor& desc, PluginMultiFrameRunFn run) {
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

    auto parallelControl = Detail::makeThreadControl(opts);

    CLI::initLogging(desc.name);

    // The window is the explicit `--frames` list when present; otherwise the
    // single positional file (single-frame == window of one).
    std::vector<std::string> frameFiles;
    const std::string framesArg = CLI::getString(opts, "--frames");
    if (!framesArg.empty()) {
        frameFiles = Detail::splitFrameList(framesArg);
    }
    if (frameFiles.empty()) {
        frameFiles.push_back(filename);
    }

    std::vector<LammpsParser::Frame> frames(frameFiles.size());
    for (std::size_t i = 0; i < frameFiles.size(); ++i) {
        if (!CLI::parseFrame(frameFiles[i], frames[i])) {
            spdlog::error("Failed to parse window frame: {}", frameFiles[i]);
            return 1;
        }
    }

    std::size_t primaryIndex = static_cast<std::size_t>(std::max(0, CLI::getInt(opts, "--primary", 0)));
    if (primaryIndex >= frames.size()) primaryIndex = 0;

    outputBase = CLI::deriveOutputBase(filename, outputBase);
    spdlog::info("Output base: {} ({} frames, primary index {})", outputBase, frames.size(), primaryIndex);

    json result = run(opts, frames, primaryIndex, outputBase);

    return Detail::reportFailure(result, desc) ? 1 : 0;
}

} // namespace Volt::Plugin

#define VOLT_PLUGIN_MAIN(descriptor, ...) \
    int main(int argc, char* argv[]) { \
        return Volt::Plugin::pluginMain(argc, argv, descriptor, __VA_ARGS__); \
    }

#define VOLT_PLUGIN_MAIN_MULTIFRAME(descriptor, ...) \
    int main(int argc, char* argv[]) { \
        return Volt::Plugin::pluginMainMultiFrame(argc, argv, descriptor, __VA_ARGS__); \
    }
