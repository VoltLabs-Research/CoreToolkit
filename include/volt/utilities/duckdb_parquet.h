#pragma once

#include <volt/core/runtime_budget.h>
#include <volt/utilities/columnar_row_writer.h>

#include <duckdb.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <string>
#include <string_view>
#include <unordered_map>
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

// Runs the CREATE TABLE of a writer's staging table, `what` the rows it stages, for the error that names the file.
inline void createTable(duckdb::Connection& con, const std::string& ddl, const std::string& what,
                        const std::string& filePath){
    auto created = con.Query(ddl);
    if(created->HasError()){
        throw std::runtime_error(
            "Failed to stage " + what + " for " + filePath + ": " + created->GetError()
        );
    }
}

inline void copyTableToParquet(duckdb::Connection& con,
                               const std::string& table,
                               const std::string& outputPath){
    const std::string sql =
        "COPY " + table + " TO " + sqlQuote(outputPath) +
        " (FORMAT PARQUET, COMPRESSION ZSTD)";
    auto result = con.Query(sql);
    if(!result->HasError()) return;

    std::error_code discard;
    std::filesystem::remove(outputPath, discard);
    throw std::runtime_error(
        "Failed to write Parquet file " + outputPath + ": " + result->GetError()
    );
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

namespace Volt {

// The columns a ColumnarRowWriter collected, kept until the writer creates its table and appends the rows.
struct ColumnarRowWriter::Impl {
    // The table's own columns: a caller's value for one of them is dropped.
    std::vector<std::string_view> reserved;
    std::vector<Detail::DynColumn> columns;
    std::unordered_map<std::string, std::size_t> index;
    std::int64_t rowsCompleted = 0;

    explicit Impl(std::vector<std::string_view> reservedColumns) : reserved(std::move(reservedColumns)) {}

    void appendDouble(const std::string& name, double value);
    void appendInt64(const std::string& name, std::int64_t value);
    void appendString(const std::string& name, const std::string& value);
    void appendList(const std::string& name, const std::vector<double>& values);
    // Gives every column this row did not write a null.
    void finishRow();

    // ", <name> <type>" for each column, after the table's own in its CREATE TABLE.
    std::string columnsDdl() const;
    void appendColumns(duckdb::Appender& appender, std::size_t row) const;

private:
    bool isReserved(const std::string& name) const;
    Detail::DynColumn& ensure(const std::string& name, Detail::ColType type);
};

}
