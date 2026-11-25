#pragma once

#include <volt/core/lammps_parser.h>
#include <volt/core/run_log.h>
#include <volt/core/runtime_budget.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace Volt::Plugin{

// The execution log of a plugin run as pluginMain drives it: the lines every run starts and ends with.
class PluginLog : public RunLog{
public:
    // The dump about to be read; its line starts the phase that reads it.
    struct InputFile{
        const std::string& path;
    };

    struct ReferenceFile{
        const std::string& path;
    };

    // The frame of --reference, or none when the run was given none and the current frame is its own reference.
    struct ReferenceFrame{
        const LammpsParser::Frame* frame;
    };

    // The run's result: whether it failed.
    struct RunResult{
        const nlohmann::json& result;
    };

    // Where the run writes its files; a window run also says how many frames it read and which one is the primary.
    struct OutputBase{
        const std::string& path;
        std::size_t windowFrames = 0;
        std::size_t primaryIndex = 0;
    };

    PluginLog() = default;
    // The description names the analysis in the run's last line.
    explicit PluginLog(std::string description) : _description(std::move(description)){}

    using RunLog::dump;

    void dump(const Runtime::ThreadBudget& budget);
    void dump(const Runtime::NumaBinding& binding);
    void dump(const InputFile& input);
    void dump(const ReferenceFile& reference);
    // A frame read: its atoms, simulation cell and columns.
    void dump(const LammpsParser::Frame& frame);
    void dump(const ReferenceFrame& reference);
    void dump(const OutputBase& outputBase);
    // Closes the run with how long it took.
    void dump(const RunResult& run);

private:
    const std::string _description;
};

}
