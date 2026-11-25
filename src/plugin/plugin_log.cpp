#include <volt/plugin/plugin_log.h>

#include <spdlog/spdlog.h>

namespace Volt::Plugin{

void PluginLog::dump(const Runtime::ThreadBudget& budget){
#if defined(VOLT_PLATFORM_WASM) || defined(__EMSCRIPTEN__)
    spdlog::info("CPU budget: {} worker thread (wasm serial runtime)", budget.workers);
#else
    spdlog::info("CPU budget: {} worker threads (oneTBB, OpenMP, geogram)", budget.workers);
    spdlog::info("DuckDB budget: {} threads, {} MiB memory limit",
                 budget.duckdbThreads, budget.duckdbMemoryLimitBytes / (1024ull * 1024ull));
#endif
}

void PluginLog::dump(const Runtime::NumaBinding& binding){
    if(binding.node < 0){
        spdlog::info("NUMA: a single node is visible, the process stays unbound");
        return;
    }
    spdlog::info("NUMA: bound to node {} of {} ({} concurrency)", binding.node, binding.nodes, binding.concurrency);
}

void PluginLog::dump(const InputFile& input){
    spdlog::info("Parsing LAMMPS file: {}", input.path);
    phaseMilliseconds();
}

void PluginLog::dump(const ReferenceFile& reference){
    spdlog::info("Parsing reference file: {}", reference.path);
    phaseMilliseconds();
}

void PluginLog::dump(const LammpsParser::Frame& frame){
    spdlog::info("Successfully loaded {} atoms (timestep {}) in {}.", frame.natoms, frame.timestep, phaseElapsed());

    const auto& cell = frame.simulationCell;
    const auto& pbc = cell.pbcFlags();
    spdlog::info("Simulation cell: {:.3f} x {:.3f} x {:.3f}, volume {:.4g}, periodic [{} {} {}]",
        cell.matrix().column(0).length(), cell.matrix().column(1).length(), cell.matrix().column(2).length(),
        cell.volume3D(), pbc[0] ? "x" : "-", pbc[1] ? "y" : "-", pbc[2] ? "z" : "-");

    std::string columns;
    for(const auto& name : frame.atomColumnOrder){
        if(!columns.empty()) columns += ' ';
        columns += name;
    }
    if(!columns.empty()){
        spdlog::info("Atom columns ({}): {}", frame.atomColumnOrder.size(), columns);
    }
}

void PluginLog::dump(const ReferenceFrame& reference){
    if(!reference.frame){
        spdlog::warn("No reference frame given (--reference); the current frame is its own reference");
        return;
    }
    spdlog::info("Reference loaded: {} atoms (timestep {}) in {}",
        reference.frame->natoms, reference.frame->timestep, phaseElapsed());
}

void PluginLog::dump(const OutputBase& outputBase){
    if(outputBase.windowFrames == 0){
        spdlog::info("Output base: {}", outputBase.path);
    }else{
        spdlog::info("Output base: {} ({} frames, primary index {})",
            outputBase.path, outputBase.windowFrames, outputBase.primaryIndex);
    }
}

void PluginLog::dump(const RunResult& run){
    if(run.result.value("is_failed", false)){
        spdlog::error("Analysis failed after {}: {}", runElapsed(), run.result.value("error", "Unknown error"));
    }else{
        spdlog::info("{} completed in {}.", _description, runElapsed());
    }
}

}
