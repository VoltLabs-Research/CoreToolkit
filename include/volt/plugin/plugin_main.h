#pragma once

#include <volt/cli/common.h>
#include <volt/core/analysis_result.h>
#include <volt/core/lammps_parser.h>
#include <volt/core/progress.h>
#include <volt/core/runtime_budget.h>
#include <volt/plugin/plugin_log.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
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

// How many worker threads a run takes when --threads is not given.
enum class DefaultThreads { Hardware, PhysicalCores };

// Whether the frame of --reference must hold as many atoms as the current frame.
enum class ReferenceAtomCount { Same, Any };

struct PluginDescriptor {
    std::string name;
    std::string description;
    std::vector<CliOption> options;
    bool needsReferenceFrame = false;
    FrameMode frameMode = FrameMode::Single;
    DescriptorRefineFn refine;
    DefaultThreads defaultThreads = DefaultThreads::Hardware;
    // Any: pluginMain also reads --reference, which the plugin declares itself, whatever its atom count (the lattice
    // sites of a Wigner-Seitz analysis). The reference is then optional, and a run without one gets no reference.
    ReferenceAtomCount referenceAtomCount = ReferenceAtomCount::Same;
    // The binary's modes besides the analysis, with arguments of their own (self-tests): pluginMain hands them the
    // arguments before it reads any, and returns the exit code of the mode they ask for.
    std::function<std::optional<int>(int argc, char* argv[])> extraModes;
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
    options.push_back({"--numa", "auto|off", "Bind the process to one NUMA node. "
        "Use 'auto' when running several plugin processes at once so they spread across "
        "nodes; 'off' (default) leaves the process free to use every node.", "off", {"auto", "off"}, ""});
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

// One line with the value every plugin option resolved to, defaults included, so the log shows
// the exact configuration of the run. Paths are left out: the input lines already name them.
inline void logParameters(const PluginDescriptor& desc, const OptsMap& opts) {
    std::string line;
    for (const auto& opt : desc.options) {
        if (opt.type == "path" || opt.type == "path-list") continue;
        const auto it = opts.find(opt.name);
        const std::string& value = it != opts.end() ? it->second : opt.defaultVal;
        if (value.empty()) continue;
        if (!line.empty()) line += ", ";
        line += opt.name.rfind("--", 0) == 0 ? opt.name.substr(2) : opt.name;
        line += '=';
        line += value;
    }
    if (!line.empty()) {
        spdlog::info("Parameters: {}", line);
    }
}

namespace Detail {

inline int defaultThreads(DefaultThreads policy) {
    const int hardware = std::thread::hardware_concurrency() > 0
        ? static_cast<int>(std::thread::hardware_concurrency()) : 1;
    const int cores = policy == DefaultThreads::PhysicalCores ? Runtime::physicalCoreCount() : 0;
    return cores > 0 ? std::min(hardware, cores) : hardware;
}

inline void applyResourceBudget(const OptsMap& opts, const PluginDescriptor& desc, PluginLog& log) {
    const int requestedThreads = std::max(1, CLI::getInt(opts, "--threads", defaultThreads(desc.defaultThreads)));
    log.dump(Runtime::applyThreadBudget(requestedThreads));

    if (CLI::getString(opts, "--numa", "off") == "auto") {
        log.dump(Runtime::bindToNumaNode());
    }
}

template <class Body>
inline auto runWithinBudget(Body&& body) -> decltype(body()) {
    decltype(body()) result{};
    Runtime::runInBoundArena([&]{ result = body(); });
    return result;
}

template <class Body>
inline json runReportingFailures(Body&& body) {
    try {
        return runWithinBudget(std::forward<Body>(body));
    } catch (const std::exception& error) {
        return json{{"is_failed", true}, {"error", error.what()}};
    } catch (...) {
        return json{{"is_failed", true}, {"error", "Unknown error"}};
    }
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

inline std::optional<int> runExtraModes(const PluginDescriptor& desc, int argc, char* argv[]) {
    return desc.extraModes ? desc.extraModes(argc, argv) : std::nullopt;
}

// Exit code of the run; only one that succeeded reports it is done.
inline int finishRun(const json& result, PluginLog& log) {
    log.dump(PluginLog::RunResult{result});
    if (result.value("is_failed", false)) return 1;
    JobProgress::instance().finish();
    return 0;
}

// The reference frame of a plugin that takes one: none when it needs no reference; the current frame itself when it
// needs one and --reference was not given; false when the reference cannot be used.
inline bool readReferenceFrame(
    const OptsMap& opts,
    const PluginDescriptor& desc,
    const LammpsParser::Frame& frame,
    LammpsParser::Frame& refFrame,
    const LammpsParser::Frame*& refFramePtr,
    PluginLog& log
) {
    refFramePtr = nullptr;
    const bool anyAtomCount = desc.referenceAtomCount == ReferenceAtomCount::Any;
    if (!desc.needsReferenceFrame && !anyAtomCount) return true;

    const std::string refFile = CLI::getString(opts, "--reference");
    if (refFile.empty()) {
        if (desc.needsReferenceFrame) {
            log.dump(PluginLog::ReferenceFrame{nullptr});
            refFramePtr = &frame;
        }
        return true;
    }

    log.dump(PluginLog::ReferenceFile{refFile});
    LammpsParser refParser;
    if (!refParser.parseFile(refFile, refFrame)) {
        spdlog::error("Failed to parse reference file: {}", refFile);
        return false;
    }
    if (!anyAtomCount && refFrame.natoms != frame.natoms) {
        spdlog::error("Atom count mismatch: current={} reference={}", frame.natoms, refFrame.natoms);
        return false;
    }
    refFramePtr = &refFrame;
    log.dump(PluginLog::ReferenceFrame{refFramePtr});
    return true;
}

// pluginMain, with the line that logs the run's parameters given by the caller.
inline int runPlugin(
    int argc,
    char* argv[],
    const PluginDescriptor& desc,
    const std::function<void(const OptsMap&)>& logRunParameters,
    const PluginRunFn& run
) {
    if (auto exitCode = runExtraModes(desc, argc, argv)) {
        return *exitCode;
    }
    if (argc < 2) {
        showPluginUsage(argv[0], desc);
        return 1;
    }

    std::string filename, outputBase;
    auto opts = CLI::parseArgs(argc, argv, filename, outputBase);

    if (auto exitCode = handleIntrospection(argv[0], desc, opts, filename)) {
        return *exitCode;
    }

    CLI::initLogging(desc.name);
    PluginLog log(desc.description);
    applyResourceBudget(opts, desc, log);
    logRunParameters(opts);
    JobProgress& progress = JobProgress::instance();
    progress.beginInput();

    LammpsParser::Frame frame;
    if (!CLI::parseFrame(filename, frame, log)) return 1;

    LammpsParser::Frame refFrame;
    const LammpsParser::Frame* refFramePtr = nullptr;
    if (!readReferenceFrame(opts, desc, frame, refFrame, refFramePtr, log)) return 1;

    outputBase = CLI::deriveOutputBase(filename, outputBase);
    log.dump(PluginLog::OutputBase{outputBase});

    progress.beginAnalysis(desc.description);
    json result = runReportingFailures([&]{ return run(opts, frame, refFramePtr, outputBase); });

    return finishRun(result, log);
}

}

inline int pluginMain(int argc, char* argv[], const PluginDescriptor& desc, PluginRunFn run) {
    return Detail::runPlugin(argc, argv, desc, [&desc](const OptsMap& opts) { logParameters(desc, opts); }, run);
}

inline int pluginMainMultiFrame(int argc, char* argv[], const PluginDescriptor& desc, PluginMultiFrameRunFn run) {
    if (auto exitCode = Detail::runExtraModes(desc, argc, argv)) {
        return *exitCode;
    }
    if (argc < 2) {
        showPluginUsage(argv[0], desc);
        return 1;
    }

    std::string filename, outputBase;
    auto opts = CLI::parseArgs(argc, argv, filename, outputBase);

    if (auto exitCode = handleIntrospection(argv[0], desc, opts, filename)) {
        return *exitCode;
    }

    CLI::initLogging(desc.name);
    PluginLog log(desc.description);
    Detail::applyResourceBudget(opts, desc, log);
    logParameters(desc, opts);
    JobProgress& progress = JobProgress::instance();

    std::vector<std::string> frameFiles;
    const std::string framesArg = CLI::getString(opts, "--frames");
    if (!framesArg.empty()) {
        frameFiles = Detail::splitFrameList(framesArg);
    }
    if (frameFiles.empty()) {
        frameFiles.push_back(filename);
    }

    progress.beginInput(frameFiles.size() == 1 ? "Reading the input frame" : "Reading " + std::to_string(frameFiles.size()) + " input frames");
    std::vector<LammpsParser::Frame> frames(frameFiles.size());
    for (std::size_t i = 0; i < frameFiles.size(); ++i) {
        if (!CLI::parseFrame(frameFiles[i], frames[i], log)) {
            spdlog::error("Failed to parse window frame: {}", frameFiles[i]);
            return 1;
        }
        progress.setFraction(static_cast<double>(i + 1) / static_cast<double>(frameFiles.size()));
    }

    std::size_t primaryIndex = static_cast<std::size_t>(std::max(0, CLI::getInt(opts, "--primary", 0)));
    if (primaryIndex >= frames.size()) primaryIndex = 0;

    outputBase = CLI::deriveOutputBase(filename, outputBase);
    log.dump(PluginLog::OutputBase{outputBase, frames.size(), primaryIndex});

    progress.beginAnalysis(desc.description);
    json result = Detail::runReportingFailures([&]{ return run(opts, frames, primaryIndex, outputBase); });

    return Detail::finishRun(result, log);
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
