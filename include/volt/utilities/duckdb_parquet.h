#pragma once

// Internal helper shared by CoreToolkit's Parquet writers. DuckDB is the Parquet
// engine: it ships prebuilt binaries on ConanCenter for every CI target (Linux,
// macOS, Windows), so it never compiles from source — unlike Arrow, which had no
// Parquet-enabled binary and both ballooned build time and failed to build on
// MSVC. DuckDB also writes the exact Parquet dialect the ClusterDaemon reads back
// (it consumes results via @duckdb/node-api), keeping writer and reader in lockstep.
//
// This header is CoreToolkit-internal: plugins never include DuckDB or this file.

#include <volt/core/runtime_budget.h>

#include <duckdb.hpp>

#include <memory>
#include <string>

namespace Volt::Detail {

// Creates the in-memory DuckDB instance the writers use, with both of its
// resource knobs set explicitly.
//
// `duckdb::DuckDB db(nullptr)` on its own takes DuckDB's defaults: threads =
// every core, and memory_limit = 80% of system RAM. Neither is a limit in this
// process — the analysis already owns a TBB pool sized to --threads, there are
// four writer sites, and the daemon runs up to (nproc - 1) plugin processes at
// once. Four instances each claiming 80% of RAM is how an out-of-memory
// condition stops being a catchable DuckDB error and becomes a kernel OOM kill,
// which in the daemon strands a queue lease.
// Set through the typed DBConfigOptions fields rather than SetOptionByName():
// `memory_limit` is a VARCHAR option ("4GB"), so handing it a BIGINT throws inside
// the DuckDB constructor and the writers report it as a plain "Failed to write".
// The struct fields take bytes and a thread count directly, with no parsing step
// to get wrong.
inline std::unique_ptr<duckdb::DuckDB> openInMemoryDb(){
    duckdb::DBConfig config;
    config.options.maximum_memory =
        static_cast<duckdb::idx_t>(Volt::Runtime::duckdbMemoryLimitBytes());
    config.options.maximum_threads =
        static_cast<duckdb::idx_t>(Volt::Runtime::duckdbThreadBudget());
    return std::make_unique<duckdb::DuckDB>(nullptr, &config);
}

// Escapes a path for safe interpolation inside a single-quoted SQL string literal
// (doubles any embedded single quote, per SQL standard).
inline std::string sqlQuote(const std::string& path){
    std::string out;
    out.reserve(path.size() + 2);
    out.push_back('\'');
    for(char c : path){
        if(c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// COPYs an existing table to a ZSTD-compressed Parquet file. Returns false on any
// DuckDB error instead of throwing, matching the writers' bool contract.
inline bool copyTableToParquet(duckdb::Connection& con,
                               const std::string& table,
                               const std::string& outputPath){
    const std::string sql =
        "COPY " + table + " TO " + sqlQuote(outputPath) +
        " (FORMAT PARQUET, COMPRESSION ZSTD)";
    auto result = con.Query(sql);
    return !result->HasError();
}

} // namespace Volt::Detail
