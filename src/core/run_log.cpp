#include <volt/core/run_log.h>

#include <volt/analysis/reference_configuration.h>
#include <volt/core/type_counts.h>
#include <volt/utilities/written_table.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace Volt{

Stopwatch::Clock::duration RunLog::phaseDuration(){
    const Stopwatch::Clock::duration duration = _phase.elapsedTime();
    _phase.restart();
    return duration;
}

long long RunLog::phaseMilliseconds(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(phaseDuration()).count();
}

std::string RunLog::phaseElapsed(){
    return formatDuration(phaseDuration());
}

void RunLog::dump(const CutoffNeighborFinder&){
    spdlog::info("Neighbor lists ready in {}", phaseElapsed());
}

void RunLog::dump(const NearestNeighborFinder&){
    spdlog::info("Neighbor lists ready in {}", phaseElapsed());
}

void RunLog::dump(const ReferenceMatch& match){
    if(!match.byIdentifier){
        return;
    }
    const std::size_t atoms = match.currentToReference.size();
    const std::size_t unmatched = static_cast<std::size_t>(
        std::ranges::count(match.currentToReference, ReferenceMatch::kNoCounterpart));
    spdlog::info("Matched {} atoms to the reference by identifier in {}", atoms - unmatched, phaseElapsed());
    if(unmatched > 0){
        spdlog::warn("{} atoms ({:.1f}%) have no counterpart in the reference; they are marked invalid",
            unmatched, percentOf(unmatched, atoms));
    }
}

void RunLog::dump(const TypeCounts& counts){
    if(counts.counts.empty()){
        return;
    }
    std::string text;
    char entry[48];
    for(const auto& [name, count] : counts.counts){
        std::snprintf(entry, sizeof(entry), " %zu (%.1f%%)", count, percentOf(count, counts.atoms));
        text += (text.empty() ? "" : ", ") + name + entry;
    }
    if(counts.share){
        spdlog::info("{}: {}; {} {} ({:.1f}%)", counts.title, text, counts.share->count, counts.share->what,
            percentOf(counts.share->count, counts.atoms));
        return;
    }
    spdlog::info("{}: {}", counts.title, text);
}

// Writing a table is a phase of its own, so the next timed line does not count it: its time is read and dropped.
void RunLog::dump(const WrittenTable& table){
    if(table.error.empty()){
        spdlog::info("{} parquet written to {}", table.contents, table.path);
    }else{
        spdlog::warn("Could not write {} parquet {}: {}", table.contents, table.path, table.error);
    }
    phaseMilliseconds();
}

void RunLog::dump(const AtomsTable& table){
    spdlog::info("Exported atoms data to: {}", table.path);
    phaseMilliseconds();
}

}
