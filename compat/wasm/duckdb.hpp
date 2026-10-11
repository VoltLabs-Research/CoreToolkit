#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace duckdb {

using idx_t = std::size_t;

enum class LogicalTypeId {
    INVALID,
    TINYINT,
    SMALLINT,
    INTEGER,
    BIGINT,
    HUGEINT,
    UTINYINT,
    USMALLINT,
    UINTEGER,
    UBIGINT,
    UHUGEINT,
    FLOAT,
    DOUBLE,
    DECIMAL,
    BOOLEAN,
    VARCHAR,
    LIST
};

class LogicalType {
public:
    LogicalType() : id_(LogicalTypeId::INVALID) {}
    explicit LogicalType(LogicalTypeId id) : id_(id) {}
    LogicalType(LogicalTypeId id, LogicalType child) : id_(id), child_(std::make_shared<LogicalType>(std::move(child))) {}

    static const LogicalType DOUBLE;
    static const LogicalType BIGINT;
    static const LogicalType VARCHAR;
    static LogicalType LIST(const LogicalType& child) { return LogicalType(LogicalTypeId::LIST, child); }

    LogicalTypeId id() const { return id_; }
    const LogicalType& child() const { return child_ ? *child_ : emptyChild(); }

    std::string ToString() const {
        switch(id_){
            case LogicalTypeId::TINYINT: return "TINYINT";
            case LogicalTypeId::SMALLINT: return "SMALLINT";
            case LogicalTypeId::INTEGER: return "INTEGER";
            case LogicalTypeId::BIGINT: return "BIGINT";
            case LogicalTypeId::HUGEINT: return "HUGEINT";
            case LogicalTypeId::UTINYINT: return "UTINYINT";
            case LogicalTypeId::USMALLINT: return "USMALLINT";
            case LogicalTypeId::UINTEGER: return "UINTEGER";
            case LogicalTypeId::UBIGINT: return "UBIGINT";
            case LogicalTypeId::UHUGEINT: return "UHUGEINT";
            case LogicalTypeId::FLOAT: return "FLOAT";
            case LogicalTypeId::DOUBLE: return "DOUBLE";
            case LogicalTypeId::DECIMAL: return "DECIMAL";
            case LogicalTypeId::BOOLEAN: return "BOOLEAN";
            case LogicalTypeId::VARCHAR: return "VARCHAR";
            case LogicalTypeId::LIST: return child().ToString() + "[]";
            case LogicalTypeId::INVALID: break;
        }
        return "INVALID";
    }

    static LogicalType fromSql(std::string sql) {
        while(!sql.empty() && sql.front() == ' ') sql.erase(sql.begin());
        while(!sql.empty() && sql.back() == ' ') sql.pop_back();
        std::string upper;
        upper.reserve(sql.size());
        for(char c : sql) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if(upper == "DOUBLE[][]") return LIST(LIST(DOUBLE));
        if(upper == "DOUBLE[]") return LIST(DOUBLE);
        if(upper == "DOUBLE") return DOUBLE;
        if(upper == "FLOAT") return LogicalType(LogicalTypeId::FLOAT);
        if(upper == "BIGINT") return BIGINT;
        if(upper == "INTEGER" || upper == "INT") return LogicalType(LogicalTypeId::INTEGER);
        if(upper == "UINTEGER") return LogicalType(LogicalTypeId::UINTEGER);
        if(upper == "UBIGINT") return LogicalType(LogicalTypeId::UBIGINT);
        if(upper == "VARCHAR") return VARCHAR;
        if(upper == "BOOLEAN") return LogicalType(LogicalTypeId::BOOLEAN);
        return VARCHAR;
    }

private:
    static const LogicalType& emptyChild() {
        static const LogicalType empty;
        return empty;
    }

    LogicalTypeId id_;
    std::shared_ptr<LogicalType> child_;
};

inline const LogicalType LogicalType::DOUBLE{LogicalTypeId::DOUBLE};
inline const LogicalType LogicalType::BIGINT{LogicalTypeId::BIGINT};
inline const LogicalType LogicalType::VARCHAR{LogicalTypeId::VARCHAR};

class Value {
public:
    Value() = default;
    explicit Value(const LogicalType& type) : type_(type), isNull_(true) {}
    explicit Value(std::string text) : type_(LogicalType::VARCHAR), string_(std::move(text)) {}
    explicit Value(const char* text) : Value(std::string(text ? text : "")) {}

    static Value DOUBLE(double value) {
        Value out;
        out.type_ = LogicalType::DOUBLE;
        out.double_ = value;
        return out;
    }

    static Value BIGINT(std::int64_t value) {
        Value out;
        out.type_ = LogicalType::BIGINT;
        out.int64_ = value;
        return out;
    }

    static Value LIST(const LogicalType& type, std::vector<Value> items) {
        Value out;
        out.type_ = type.id() == LogicalTypeId::LIST ? type : LogicalType::LIST(type);
        out.list_ = std::move(items);
        return out;
    }

    bool isNull() const { return isNull_; }
    const LogicalType& type() const { return type_; }
    double asDouble() const { return double_; }
    std::int64_t asInt64() const { return int64_; }
    const std::string& asString() const { return string_; }
    std::string ToString() const {
        if(isNull_) return "NULL";
        switch(type_.id()){
            case LogicalTypeId::VARCHAR:
                return string_;
            case LogicalTypeId::DOUBLE:
            case LogicalTypeId::FLOAT:
            case LogicalTypeId::DECIMAL:
                return std::to_string(double_);
            case LogicalTypeId::LIST:
                return toJson().dump();
            default:
                return std::to_string(int64_);
        }
    }
    const std::vector<Value>& asList() const { return list_; }

    nlohmann::json toJson() const {
        if(isNull_) return nullptr;
        switch(type_.id()){
            case LogicalTypeId::DOUBLE:
            case LogicalTypeId::FLOAT:
            case LogicalTypeId::DECIMAL:
                return double_;
            case LogicalTypeId::VARCHAR:
                return string_;
            case LogicalTypeId::LIST: {
                nlohmann::json array = nlohmann::json::array();
                for(const auto& item : list_) array.push_back(item.toJson());
                return array;
            }
            default:
                return int64_;
        }
    }

    static Value fromJson(const nlohmann::json& json, const LogicalType& type) {
        if(json.is_null()) return Value(type);
        if(type.id() == LogicalTypeId::LIST) {
            std::vector<Value> items;
            if(json.is_array()) {
                for(const auto& item : json) items.push_back(fromJson(item, type.child()));
            }
            return LIST(type, std::move(items));
        }
        if(type.id() == LogicalTypeId::VARCHAR) {
            return Value(json.is_string() ? json.get<std::string>() : json.dump());
        }
        if(type.id() == LogicalTypeId::DOUBLE || type.id() == LogicalTypeId::FLOAT || type.id() == LogicalTypeId::DECIMAL) {
            return DOUBLE(json.get<double>());
        }
        Value out;
        out.type_ = type;
        out.int64_ = json.get<std::int64_t>();
        return out;
    }

private:
    LogicalType type_ = LogicalType::DOUBLE;
    bool isNull_ = false;
    double double_ = 0.0;
    std::int64_t int64_ = 0;
    std::string string_;
    std::vector<Value> list_;
};

struct SelectionVector {
    idx_t get_index(idx_t row) const { return row; }
};

struct ValidityMask {
    explicit ValidityMask(const std::vector<std::uint8_t>* bits = nullptr) : bits_(bits) {}
    bool RowIsValid(idx_t index) const { return !bits_ || (*bits_)[index] != 0; }

private:
    const std::vector<std::uint8_t>* bits_ = nullptr;
};

struct UnifiedVectorFormat {
    const SelectionVector* sel = nullptr;
    ValidityMask validity;
    const void* data = nullptr;

    template<typename T>
    static const T* GetData(const UnifiedVectorFormat& format) {
        return static_cast<const T*>(format.data);
    }
};

class Vector {
public:
    Vector() = default;
    explicit Vector(LogicalType type) : type_(std::move(type)) {}

    const LogicalType& GetType() const { return type_; }

    void setValues(std::vector<Value> values) {
        values_ = std::move(values);
        rebuildStorage();
    }

    const Value& valueAt(idx_t row) const { return values_[row]; }

    void ToUnifiedFormat(idx_t, UnifiedVectorFormat& format) const {
        format.sel = &selection_;
        format.validity = ValidityMask(&valid_);
        format.data = storage_.empty() ? nullptr : storage_.data();
    }

private:
    static std::size_t widthOf(LogicalTypeId id) {
        switch(id){
            case LogicalTypeId::TINYINT:
            case LogicalTypeId::UTINYINT:
            case LogicalTypeId::BOOLEAN:
                return 1;
            case LogicalTypeId::SMALLINT:
            case LogicalTypeId::USMALLINT:
                return 2;
            case LogicalTypeId::INTEGER:
            case LogicalTypeId::UINTEGER:
            case LogicalTypeId::FLOAT:
                return 4;
            default:
                return 8;
        }
    }

    void writeNumeric(idx_t row, const Value& value) {
        auto* dest = storage_.data() + row * stride_;
        switch(type_.id()){
            case LogicalTypeId::TINYINT: {
                const auto n = static_cast<std::int8_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::UTINYINT: {
                const auto n = static_cast<std::uint8_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::SMALLINT: {
                const auto n = static_cast<std::int16_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::USMALLINT: {
                const auto n = static_cast<std::uint16_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::INTEGER: {
                const auto n = static_cast<std::int32_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::UINTEGER: {
                const auto n = static_cast<std::uint32_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::BIGINT:
            case LogicalTypeId::HUGEINT: {
                const auto n = value.asInt64();
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::UBIGINT:
            case LogicalTypeId::UHUGEINT: {
                const auto n = static_cast<std::uint64_t>(value.asInt64());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::FLOAT: {
                const auto n = static_cast<float>(value.asDouble());
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::DOUBLE:
            case LogicalTypeId::DECIMAL: {
                const auto n = value.asDouble();
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            case LogicalTypeId::BOOLEAN: {
                const auto n = static_cast<std::uint8_t>(value.asInt64() ? 1 : 0);
                std::memcpy(dest, &n, sizeof(n));
                break;
            }
            default:
                break;
        }
    }

    void rebuildStorage() {
        valid_.assign(values_.size(), 0);
        stride_ = widthOf(type_.id());
        storage_.assign(values_.size() * stride_, 0);
        for(idx_t i = 0; i < values_.size(); ++i){
            const auto& value = values_[i];
            if(value.isNull()) continue;
            valid_[i] = 1;
            writeNumeric(i, value);
        }
    }

    LogicalType type_;
    std::vector<Value> values_;
    std::vector<std::uint8_t> valid_;
    std::vector<std::uint8_t> storage_;
    std::size_t stride_ = 8;
    SelectionVector selection_{};
};

class DataChunk {
public:
    idx_t size() const { return count_; }
    std::vector<Vector> data;

    void setCount(idx_t count) { count_ = count; }

private:
    idx_t count_ = 0;
};

class QueryResult {
public:
    bool HasError() const { return !error_.empty(); }
    const std::string& GetError() const { return error_; }
    idx_t RowCount() const { return rowCount_; }
    idx_t ColumnCount() const { return names.size(); }

    std::vector<std::string> names;
    std::vector<LogicalType> types;

    std::unique_ptr<DataChunk> Fetch() {
        if(fetched_ || HasError()) return nullptr;
        fetched_ = true;
        auto chunk = std::make_unique<DataChunk>();
        chunk->setCount(rowCount_);
        chunk->data = columns_;
        return chunk;
    }

    Value GetValue(idx_t column, idx_t row) const {
        if(column >= columns_.size() || row >= rowCount_) return Value();
        return columns_[column].valueAt(row);
    }

    void fail(std::string error) { error_ = std::move(error); }

    void setColumns(std::vector<std::string> columnNames,
                    std::vector<LogicalType> columnTypes,
                    std::vector<std::vector<Value>> rows) {
        names = std::move(columnNames);
        types = std::move(columnTypes);
        rowCount_ = rows.size();
        columns_.resize(names.size());
        for(idx_t column = 0; column < names.size(); ++column){
            columns_[column] = Vector(types[column]);
            std::vector<Value> values;
            values.reserve(rows.size());
            for(const auto& row : rows) values.push_back(row[column]);
            columns_[column].setValues(std::move(values));
        }
    }

private:
    std::string error_;
    idx_t rowCount_ = 0;
    bool fetched_ = false;
    std::vector<Vector> columns_;
};

struct Table {
    std::vector<std::string> names;
    std::vector<LogicalType> types;
    std::vector<std::vector<Value>> rows;
};

class Connection;

class DuckDB {
public:
    explicit DuckDB(const char* = nullptr, struct DBConfig* = nullptr) {}
    std::unordered_map<std::string, Table> tables;
};

struct DBConfig {
    struct Options {
        idx_t maximum_memory = 0;
        idx_t maximum_threads = 1;
    } options;
};

class Appender {
public:
    Appender(Connection& connection, std::string tableName);

    void BeginRow() { current_.clear(); }
    void EndRow();
    void Close() {}

    template<typename T>
    void Append(T value) {
        current_.push_back(makeValue(value));
    }

    void Append(const Value& value) { current_.push_back(value); }

private:
    template<typename T>
    Value makeValue(T value) {
        if constexpr(std::is_same_v<T, double> || std::is_same_v<T, float>) {
            return Value::DOUBLE(static_cast<double>(value));
        } else {
            Value out = Value::BIGINT(static_cast<std::int64_t>(value));
            return out;
        }
    }

    DuckDB* db_ = nullptr;
    std::string tableName_;
    std::vector<Value> current_;
};

class Connection {
public:
    explicit Connection(DuckDB& db) : db_(&db) {}

    std::unique_ptr<QueryResult> Query(const std::string& sql);

    DuckDB* db() { return db_; }

private:
    DuckDB* db_;
};

inline std::string unquoteIdent(std::string text) {
    while(!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\t')) text.erase(text.begin());
    while(!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\t')) text.pop_back();
    if(text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

inline bool parseCreateTable(const std::string& sql, Table& table, std::string& error) {
    const auto open = sql.find('(');
    const auto close = sql.rfind(')');
    if(open == std::string::npos || close == std::string::npos || close <= open) {
        error = "invalid CREATE TABLE";
        return false;
    }
    const std::string header = sql.substr(0, open);
    const auto namePos = header.find_last_of(" \t");
    table.names.clear();
    table.types.clear();
    table.rows.clear();
    if(namePos == std::string::npos) {
        error = "missing table name";
        return false;
    }

    std::string body = sql.substr(open + 1, close - open - 1);
    std::string column;
    int depth = 0;
    auto flush = [&]() {
        if(column.empty()) return;
        std::string name;
        std::string type;
        bool inQuotes = false;
        std::size_t split = std::string::npos;
        for(std::size_t i = 0; i < column.size(); ++i){
            if(column[i] == '"') inQuotes = !inQuotes;
            if(!inQuotes && column[i] == ' ') {
                split = i;
                break;
            }
        }
        if(split == std::string::npos) {
            name = unquoteIdent(column);
            type = "VARCHAR";
        } else {
            name = unquoteIdent(column.substr(0, split));
            type = column.substr(split + 1);
        }
        table.names.push_back(name);
        table.types.push_back(LogicalType::fromSql(type));
        column.clear();
    };
    for(char c : body){
        if(c == '(') ++depth;
        if(c == ')') --depth;
        if(c == ',' && depth == 0) {
            flush();
            continue;
        }
        column.push_back(c);
    }
    flush();
    return !table.names.empty();
}

inline std::string extractQuoted(const std::string& sql) {
    const auto first = sql.find('\'');
    const auto last = sql.rfind('\'');
    if(first == std::string::npos || last == std::string::npos || last <= first) return {};
    std::string out;
    for(std::size_t i = first + 1; i < last; ++i){
        if(sql[i] == '\'' && i + 1 < last && sql[i + 1] == '\'') {
            out.push_back('\'');
            ++i;
        } else {
            out.push_back(sql[i]);
        }
    }
    return out;
}

inline bool writeTableJson(const Table& table, const std::string& path, std::string& error) {
    nlohmann::json document;
    document["columns"] = nlohmann::json::array();
    for(std::size_t i = 0; i < table.names.size(); ++i){
        document["columns"].push_back({
            {"name", table.names[i]},
            {"type", table.types[i].ToString()}
        });
    }
    document["rows"] = nlohmann::json::array();
    for(const auto& row : table.rows){
        nlohmann::json jsonRow = nlohmann::json::array();
        for(const auto& value : row) jsonRow.push_back(value.toJson());
        document["rows"].push_back(std::move(jsonRow));
    }
    std::ofstream out(path);
    if(!out) {
        error = "failed to open " + path;
        return false;
    }
    out << document.dump();
    return static_cast<bool>(out);
}

inline bool readTableJson(const std::string& path, Table& table, std::string& error) {
    std::ifstream in(path);
    if(!in) {
        error = "failed to open " + path;
        return false;
    }
    nlohmann::json document;
    try {
        in >> document;
    } catch(const std::exception& exception) {
        error = exception.what();
        return false;
    }
    table.names.clear();
    table.types.clear();
    table.rows.clear();
    for(const auto& column : document["columns"]){
        table.names.push_back(column.at("name").get<std::string>());
        table.types.push_back(LogicalType::fromSql(column.at("type").get<std::string>()));
    }
    for(const auto& jsonRow : document["rows"]){
        std::vector<Value> row;
        row.reserve(table.names.size());
        for(std::size_t i = 0; i < table.names.size(); ++i){
            row.push_back(Value::fromJson(jsonRow.at(i), table.types[i]));
        }
        table.rows.push_back(std::move(row));
    }
    return true;
}

inline std::unique_ptr<QueryResult> Connection::Query(const std::string& sql) {
    auto result = std::make_unique<QueryResult>();
    std::string error;

    const auto startsWith = [&](const char* prefix) {
        return sql.rfind(prefix, 0) == 0 || sql.find(prefix) != std::string::npos;
    };

    if(sql.find("CREATE TABLE") != std::string::npos) {
        Table table;
        if(!parseCreateTable(sql, table, error)) {
            result->fail(error);
            return result;
        }
        const auto open = sql.find('(');
        std::string header = sql.substr(0, open);
        const auto namePos = header.find_last_of(" \t");
        const std::string name = unquoteIdent(header.substr(namePos + 1));
        db_->tables[name] = std::move(table);
        return result;
    }

    if(sql.find("COPY ") != std::string::npos) {
        const auto toPos = sql.find(" TO ");
        if(toPos == std::string::npos) {
            result->fail("invalid COPY");
            return result;
        }
        std::string name = unquoteIdent(sql.substr(5, toPos - 5));
        const std::string path = extractQuoted(sql);
        auto it = db_->tables.find(name);
        if(it == db_->tables.end()) {
            result->fail("unknown table " + name);
            return result;
        }
        if(!writeTableJson(it->second, path, error)) result->fail(error);
        return result;
    }

    if(sql.find("read_parquet") != std::string::npos) {
        Table table;
        const std::string path = extractQuoted(sql);
        if(!readTableJson(path, table, error)) {
            result->fail(error);
            return result;
        }
        result->setColumns(table.names, table.types, table.rows);
        return result;
    }

    result->fail("unsupported SQL in wasm duckdb shim");
    return result;
}

inline Appender::Appender(Connection& connection, std::string tableName)
    : db_(connection.db()), tableName_(std::move(tableName)) {}

inline Value coerceValue(Value value, const LogicalType& type) {
    if(value.isNull()) return Value(type);
    switch(type.id()){
        case LogicalTypeId::DOUBLE:
        case LogicalTypeId::FLOAT:
        case LogicalTypeId::DECIMAL:
            if(value.type().id() == LogicalTypeId::DOUBLE || value.type().id() == LogicalTypeId::FLOAT) {
                return type.id() == LogicalTypeId::FLOAT ? Value::DOUBLE(value.asDouble()) : Value::DOUBLE(value.asDouble());
            }
            return Value::DOUBLE(static_cast<double>(value.asInt64()));
        case LogicalTypeId::VARCHAR:
            return value.type().id() == LogicalTypeId::VARCHAR ? value : Value(value.asString());
        case LogicalTypeId::LIST:
            return value;
        default: {
            Value out;
            if(value.type().id() == LogicalTypeId::DOUBLE || value.type().id() == LogicalTypeId::FLOAT) {
                out = Value::BIGINT(static_cast<std::int64_t>(value.asDouble()));
            } else if(value.type().id() == LogicalTypeId::VARCHAR) {
                out = value;
            } else {
                out = Value::BIGINT(value.asInt64());
            }
            return out;
        }
    }
}

inline void Appender::EndRow() {
    auto it = db_->tables.find(tableName_);
    if(it == db_->tables.end()) throw std::runtime_error("unknown table " + tableName_);
    auto& table = it->second;
    while(current_.size() < table.types.size()) current_.emplace_back(table.types[current_.size()]);
    for(std::size_t i = 0; i < table.types.size(); ++i){
        current_[i] = coerceValue(std::move(current_[i]), table.types[i]);
    }
    table.rows.push_back(std::move(current_));
    current_.clear();
}

}
