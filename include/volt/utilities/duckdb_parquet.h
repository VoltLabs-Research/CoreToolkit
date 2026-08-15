#pragma once

#include <volt/core/runtime_budget.h>

#include <duckdb.hpp>

#include <memory>
#include <string>

namespace Volt::Detail {

inline std::unique_ptr<duckdb::DuckDB> openInMemoryDb(){
    duckdb::DBConfig config;
    config.options.maximum_memory =
        static_cast<duckdb::idx_t>(Volt::Runtime::duckdbMemoryLimitBytes());
    config.options.maximum_threads =
        static_cast<duckdb::idx_t>(Volt::Runtime::duckdbThreadBudget());
    return std::make_unique<duckdb::DuckDB>(nullptr, &config);
}

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

inline bool copyTableToParquet(duckdb::Connection& con,
                               const std::string& table,
                               const std::string& outputPath){
    const std::string sql =
        "COPY " + table + " TO " + sqlQuote(outputPath) +
        " (FORMAT PARQUET, COMPRESSION ZSTD)";
    auto result = con.Query(sql);
    return !result->HasError();
}

}
