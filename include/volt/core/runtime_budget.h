#pragma once

// The one place this process decides how much of the host it may use.
//
// A plugin process owns more than one thread pool, and before this module they
// were sized independently — each one defaulting to "all cores":
//
//   * oneTBB              -> tbb::global_control, set from --threads
//   * OpenMP (geogram)    -> never set, so libgomp defaulted to nproc
//   * geogram's PDEL      -> GEO::Process::set_max_threads(min(nproc, 16)),
//                            because setMaxDelaunayThreads() had no callers
//   * DuckDB (Parquet)    -> default threads = nproc, memory_limit = 80% of RAM,
//                            once per writer instance
//
// The daemon runs up to (nproc - 1) of these processes at a time and hands each
// one a --threads budget of (nproc - 1), so on a 16-thread host the three
// unbounded pools multiplied out to several hundred software threads and several
// times physical RAM in nominal memory budgets. Measured effect: PTM reached
// 2.7x on 16 threads while the box thrashed, and a stall in the allocator turned
// roughly one frame in 12 into a multi-minute straggler.
//
// So: one number, resolved once from --threads, applied to every pool that can
// be configured centrally, and readable by the pools that must be configured
// later (geogram only after GEO::initialize(), DuckDB per connection).

#include <cstddef>
#include <functional>

// No TBB types appear below on purpose. `oneapi::tbb` is a namespace *alias* for
// `::tbb`, so forward-declaring anything inside it makes every later TBB include
// fail with "conflicts with a previous declaration"; and `tbb::task_arena` is a
// using-declaration pulled from tbb::detail::d1, so it cannot be forward-declared
// either. The arena stays behind runInBoundArena().

namespace Volt::Runtime{

// Resolves and applies the process-wide CPU budget. Call once, early, before any
// parallel work. `requestedWorkers` <= 0 means "use hardware_concurrency".
//
// Applies immediately to oneTBB (max_allowed_parallelism) and OpenMP
// (omp_set_num_threads); records the value for threadBudget() so the pools that
// cannot be set from here read the same number.
void applyThreadBudget(int requestedWorkers);

// The resolved budget. Safe before applyThreadBudget() — falls back to
// hardware_concurrency so a caller that forgot to apply it is merely
// unconstrained, never zero.
int threadBudget();

// Binds this process to one NUMA node, chosen so that sibling plugin processes
// spread across nodes rather than all landing on node 0. Returns the node id, or
// -1 when the host has a single node or binding is unavailable — in which case
// nothing is constrained and the caller should just run normally.
//
// Not automatic: a single interactive run wants every core, and constraining it
// to one node would halve it for no reason. The daemon, which runs many
// processes at once, is the caller that benefits.
int bindToNumaNode();

// Runs `work` inside the arena bindToNumaNode() created, so its worker threads
// and the pages they touch stay on the bound node. When this process is unbound
// — the default, and always on a single-node host — it is a plain call.
void runInBoundArena(const std::function<void()>& work);

// Thread count for DuckDB's own pool. Kept strictly inside the CPU budget: the
// Parquet writers run while TBB work may still be in flight.
int duckdbThreadBudget();

// Explicit ceiling for DuckDB's `memory_limit`, in bytes.
//
// DuckDB's default is 80% of system RAM *per instance*. With four writer sites
// per process and up to (nproc - 1) processes, that default is not a limit at
// all — it converts an out-of-memory condition from a catchable error into a
// kernel OOM kill, which in the daemon strands a queue lease for an hour.
//
// Override with VOLT_DUCKDB_MEMORY_LIMIT_MB (the daemon should set it from its
// own per-process budget); otherwise a conservative fraction of RAM.
std::size_t duckdbMemoryLimitBytes();

}
