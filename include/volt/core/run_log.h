#pragma once

#include <volt/core/progress.h>

#include <string>

namespace Volt{

class CutoffNeighborFinder;
class NearestNeighborFinder;
struct AtomsTable;
struct ReferenceMatch;
struct TypeCounts;
struct WrittenTable;

// The base of a package's execution log. The package's log derives from it and adds one dump() overload per result
// its algorithm builds, bringing these in with `using RunLog::dump;`. The algorithm hands each result to dump() once
// the phase that built it is over, so it keeps no timers and no strings of its own.
class RunLog{
public:
    void dump(const CutoffNeighborFinder& neighborLists);
    void dump(const NearestNeighborFinder& neighborLists);
    // Matched by identifier, with the current atoms missing from the reference if any.
    void dump(const ReferenceMatch& match);
    void dump(const TypeCounts& counts);
    void dump(const WrittenTable& table);
    void dump(const AtomsTable& table);

protected:
    // How long the phase that just ended took: the time since the previous dump. Each call starts the next phase.
    // A line that sums several phases adds their durations, which do not round to milliseconds.
    Stopwatch::Clock::duration phaseDuration();
    long long phaseMilliseconds();
    std::string phaseElapsed();

    long long runMilliseconds() const{ return _run.milliseconds(); }
    std::string runElapsed() const{ return _run.elapsed(); }

private:
    const Stopwatch _run;
    Stopwatch _phase;
};

}
