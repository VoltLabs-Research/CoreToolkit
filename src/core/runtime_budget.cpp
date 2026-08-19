#include <volt/core/runtime_budget.h>

#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/task_arena.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <functional>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#endif

namespace Volt::Runtime{

static int g_workers = 0;

std::optional<tbb::global_control> g_tbbControl;

std::optional<tbb::task_arena> g_numaArena;

int hardwareThreads(){
    const unsigned int n = std::thread::hardware_concurrency();
    return n > 0 ? static_cast<int>(n) : 1;
}

long long positiveEnv(const char* name){
    const char* raw = std::getenv(name);
    if(!raw || !*raw){
        return 0;
    }
    try{
        const long long value = std::stoll(std::string(raw));
        return value > 0 ? value : 0;
    }catch(...){
        return 0;
    }
}

std::size_t totalSystemMemoryBytes(){
#if defined(__linux__) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if(pages > 0 && pageSize > 0){
        return static_cast<std::size_t>(pages) * static_cast<std::size_t>(pageSize);
    }
#endif
    return 0;
}

void applyThreadBudget(int requestedWorkers){
    g_workers = requestedWorkers > 0 ? requestedWorkers : hardwareThreads();

    g_tbbControl.emplace(
        tbb::global_control::max_allowed_parallelism,
        static_cast<std::size_t>(g_workers)
    );

#ifdef _OPENMP
    omp_set_num_threads(g_workers);
    omp_set_dynamic(0);
#endif

    spdlog::info("CPU budget: {} worker threads (oneTBB, OpenMP, geogram)", g_workers);
    spdlog::info("DuckDB budget: {} threads, {} MiB memory limit",
                 duckdbThreadBudget(), duckdbMemoryLimitBytes() / (1024ull * 1024ull));
}

int threadBudget(){
    return g_workers > 0 ? g_workers : hardwareThreads();
}

int bindToNumaNode(){
    const std::vector<tbb::numa_node_id> nodes = tbb::info::numa_nodes();
    if(nodes.size() <= 1){
        return -1;
    }

    std::size_t which = 0;
#if defined(__linux__)
    which = static_cast<std::size_t>(::getpid()) % nodes.size();
#endif
    const tbb::numa_node_id node = nodes[which];

    tbb::task_arena::constraints limits{};
    limits.numa_id = node;
    g_numaArena.emplace(limits);
    g_numaArena->initialize();

    spdlog::info("NUMA: bound to node {} of {} ({} concurrency)",
                 node, nodes.size(), g_numaArena->max_concurrency());
    return static_cast<int>(node);
}

void runInBoundArena(const std::function<void()>& work){
    if(g_numaArena){
        g_numaArena->execute(work);
        return;
    }
    work();
}

int duckdbThreadBudget(){
    constexpr std::size_t kMebi = 1024ull * 1024ull;
    constexpr std::size_t kScanBudgetPerThreadMib = 32;

    const int cpuThreads = std::max(1, threadBudget());
    const std::size_t limitMib = duckdbMemoryLimitBytes() / kMebi;
    const auto memoryThreads = static_cast<int>(
        std::max<std::size_t>(1, limitMib / kScanBudgetPerThreadMib)
    );
    return std::min(cpuThreads, memoryThreads);
}

std::size_t duckdbMemoryLimitBytes(){
    constexpr std::size_t kMebi = 1024ull * 1024ull;

    if(const long long override_mb = positiveEnv("VOLT_DUCKDB_MEMORY_LIMIT_MB")){
        return static_cast<std::size_t>(override_mb) * kMebi;
    }

    const std::size_t total = totalSystemMemoryBytes();
    const std::size_t quarter = total > 0 ? total / 4 : 2048ull * kMebi;
    return std::min<std::size_t>(quarter, 4096ull * kMebi);
}

}
