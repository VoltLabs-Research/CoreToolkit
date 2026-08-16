#pragma once

#include <volt/core/runtime_budget.h>

#include <duckdb.hpp>

#include <memory>
#include <string>
#include <vector>

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

enum class ColType { Double, Int64, String, ListDouble };

struct DynColumn {
    std::string name;
    ColType type;
    std::vector<duckdb::Value> values;
    bool touchedThisRow = false;
};

inline duckdb::LogicalType logicalTypeFor(ColType type){
    switch(type){
        case ColType::Double:     return duckdb::LogicalType::DOUBLE;
        case ColType::Int64:      return duckdb::LogicalType::BIGINT;
        case ColType::String:     return duckdb::LogicalType::VARCHAR;
        case ColType::ListDouble: return duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE);
    }
    return duckdb::LogicalType::DOUBLE;
}

inline const char* sqlTypeFor(ColType type){
    switch(type){
        case ColType::Double:     return "DOUBLE";
        case ColType::Int64:      return "BIGINT";
        case ColType::String:     return "VARCHAR";
        case ColType::ListDouble: return "DOUBLE[]";
    }
    return "DOUBLE";
}

inline std::string quoteIdent(const std::string& name){
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back('"');
    for(char c : name){
        if(c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

}
