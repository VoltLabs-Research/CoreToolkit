#pragma once

// Internal helper shared by CoreToolkit's Parquet writers. DuckDB is the Parquet
// engine: it ships prebuilt binaries on ConanCenter for every CI target (Linux,
// macOS, Windows), so it never compiles from source — unlike Arrow, which had no
// Parquet-enabled binary and both ballooned build time and failed to build on
// MSVC. DuckDB also writes the exact Parquet dialect the ClusterDaemon reads back
// (it consumes results via @duckdb/node-api), keeping writer and reader in lockstep.
//
// This header is CoreToolkit-internal: plugins never include DuckDB or this file.

#include <duckdb.hpp>

#include <string>

namespace Volt::Detail {

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
