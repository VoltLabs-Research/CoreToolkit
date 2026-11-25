#pragma once

#include <cstddef>
#include <functional>

namespace Volt::Runtime{

// The worker threads a run takes, and the share of them and of the memory its DuckDB connections take.
struct ThreadBudget{
    int workers;
    int duckdbThreads;
    std::size_t duckdbMemoryLimitBytes;
};

ThreadBudget applyThreadBudget(int requestedWorkers);

// The physical cores of this machine, hyperthreads not counted, or 0 when the system does not say.
int physicalCoreCount();

int threadBudget();

// The NUMA node the process was bound to, of how many nodes, and how many threads its arena runs; node -1 when a
// single node is visible and the process stays unbound.
struct NumaBinding{
    int node;
    std::size_t nodes;
    int concurrency;
};

NumaBinding bindToNumaNode();

void runInBoundArena(const std::function<void()>& work);

int duckdbThreadBudget();

std::size_t duckdbMemoryLimitBytes();

namespace RuntimeDetail{

int hardwareThreads();
// The value of a positive integer environment variable; 0 when it is unset, not a number, or not positive.
long long positiveEnv(const char* name);
// 0 where the platform does not say.
std::size_t totalSystemMemoryBytes();

}

}
