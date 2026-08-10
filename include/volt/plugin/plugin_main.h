#pragma once

#include <volt/cli/common.h>
#include <volt/core/analysis_result.h>
#include <volt/core/lammps_parser.h>
#include <oneapi/tbb/global_control.h>

#include <functional>
#include <iostream>
#include <optional>
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
    std::vector<std::string> values;
    std::string bundleDefault;
};

enum class FrameMode {
    Single,
    ReferencePair,
    Window,
    All
};

using DescriptorRefineFn = std::function<void(std::vector<CliOption>&, const OptsMap&)>;

struct PluginDescriptor {
    std::string name;
    std::string description;
    std::vector<CliOption> options;
    bool needsReferenceFrame = false;
    FrameMode frameMode = FrameMode::Single;
    DescriptorRefineFn refine;
};

using PluginRunFn = std::function<json(
    const std::map<std::string, std::string>& opts,
    const LammpsParser::Frame& frame,
    const LammpsParser::Frame* refFrame,
    const std::string& outputBase
)>;

using PluginMultiFrameRunFn = std::function<json(
    const std::map<std::string, std::string>& opts,
    const std::vector<LammpsParser::Frame>& frames,
    std::size_t primaryIndex,
    const std::string& outputBase
)>;

inline constexpr int kDescriptorVersion = 1;

inline std::string frameModeName(FrameMode mode) {
    switch (mode) {
        case FrameMode::ReferencePair: return "referencePair";
        case FrameMode::Window:        return "window";
        case FrameMode::All:           return "all";
        case FrameMode::Single:        break;
    }
    return "single";
}

inline std::vector<CliOption> effectiveOptions(const PluginDescriptor& desc) {
    std::vector<CliOption> options = desc.options;
    if (desc.needsReferenceFrame) {
        options.push_back({"--reference", "path", "Reference LAMMPS dump file.", "", {}, ""});
    }
    if (desc.frameMode != FrameMode::Single) {
        options.push_back({"--frames", "path-list", "Comma/space-separated window dump files.", "", {}, ""});
        options.push_back({"--primary", "int", "Index of the primary frame in the window.", "0", {}, ""});
    }
    options.push_back({"--threads", "int", "Max worker threads.", "auto", {}, ""});
    return options;
}

inline json describePlugin(const PluginDescriptor& desc, const OptsMap& opts = {}) {
    auto resolved = effectiveOptions(desc);
    if (desc.refine) {
        desc.refine(resolved, opts);
    }
    json options = json::array();
    for (const auto& opt : resolved) {
        json entry{
            {"flag", opt.name},
            {"type", opt.type},
            {"help", opt.help},
        };
        if (!opt.defaultVal.empty())   entry["default"] = opt.defaultVal;
        if (!opt.values.empty())       entry["values"] = opt.values;
        if (!opt.bundleDefault.empty()) entry["bundleDefault"] = opt.bundleDefault;
        options.push_back(std::move(entry));
    }
    return json{
        {"descriptor", kDescriptorVersion},
        {"name", desc.name},
        {"description", desc.description},
        {"frameMode", frameModeName(desc.frameMode)},
        {"needsReferenceFrame", desc.needsReferenceFrame},
        {"positional", json::array({"input", "output_base"})},
        {"options", std::move(options)},
    };
}

inline void showPluginUsage(const std::string& argv0, const PluginDescriptor& desc) {
    CLI::printUsageHeader(argv0, "Volt - " + desc.description);
    for (const auto& opt : effectiveOptions(desc)) {
        std::cerr << "  " << opt.name << " <" << opt.type << ">";
        const std::size_t pad = (opt.name.size() + opt.type.size() + 4 < 32)
            ? (32 - opt.name.size() - opt.type.size() - 4) : 2;
        std::cerr << std::string(pad, ' ') << opt.help;
        if (!opt.values.empty()) {
            std::cerr << " (";
            for (std::size_t i = 0; i < opt.values.size(); ++i)
                std::cerr << (i ? "|" : "") << opt.values[i];
            std::cerr << ")";
        }
        if (!opt.defaultVal.empty())
            std::cerr << " [default: " << opt.defaultVal << "]";
        std::cerr << "\n";
    }
    std::cerr << "  --describe                     Print this plugin's option table as JSON and exit.\n";
    CLI::printHelpOption();
}

inline std::optional<int> handleIntrospection(
    const std::string& argv0,
    const PluginDescriptor& desc,
    const OptsMap& opts,
    const std::string& filename
) {
    if (CLI::hasOption(opts, "--describe")) {
        std::cout << describePlugin(desc, opts).dump(2) << '\n';
        return 0;
    }
    if (CLI::hasOption(opts, "--help") || filename.empty()) {
        showPluginUsage(argv0, desc);
        return filename.empty() ? 1 : 0;
    }
    return std::nullopt;
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

}

inline int pluginMain(int argc, char* argv[], const PluginDescriptor& desc, PluginRunFn run) {
    if (argc < 2) {
        showPluginUsage(argv[0], desc);
        return 1;
    }

    std::string filename, outputBase;
    auto opts = CLI::parseArgs(argc, argv, filename, outputBase);

    if (auto exitCode = handleIntrospection(argv[0], desc, opts, filename)) {
        return *exitCode;
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

inline int pluginMainMultiFrame(int argc, char* argv[], const PluginDescriptor& desc, PluginMultiFrameRunFn run) {
    if (argc < 2) {
        showPluginUsage(argv[0], desc);
        return 1;
    }

    std::string filename, outputBase;
    auto opts = CLI::parseArgs(argc, argv, filename, outputBase);

    if (auto exitCode = handleIntrospection(argv[0], desc, opts, filename)) {
        return *exitCode;
    }

    auto parallelControl = Detail::makeThreadControl(opts);

    CLI::initLogging(desc.name);

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

}

#define VOLT_PLUGIN_MAIN(descriptor, ...) \
    int main(int argc, char* argv[]) { \
        return Volt::Plugin::pluginMain(argc, argv, descriptor, __VA_ARGS__); \
    }

#define VOLT_PLUGIN_MAIN_MULTIFRAME(descriptor, ...) \
    int main(int argc, char* argv[]) { \
        return Volt::Plugin::pluginMainMultiFrame(argc, argv, descriptor, __VA_ARGS__); \
    }
