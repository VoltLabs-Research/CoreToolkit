#pragma once

// The DuckDB C++ API a WebAssembly build compiles against: a client of the DuckDB the program runs
// with, duckdb-wasm's Node build (duckdb-host.js), which executes every query. Results come back to
// the classes below, which hold them as the real API's do.

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

extern "C" {
int volt_duckdb_open();
void volt_duckdb_close(int database);
int volt_duckdb_connect(int database);
void volt_duckdb_disconnect(int connection);
char* volt_duckdb_query(int connection, const char* sql);
char* volt_duckdb_append(int connection, const char* table, const char* csv, std::size_t length);
}

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
        if(upper.size() > 2 && upper.compare(upper.size() - 2, 2, "[]") == 0) {
            return LIST(fromSql(upper.substr(0, upper.size() - 2)));
        }
        static const std::pair<const char*, LogicalTypeId> names[] = {
            {"TINYINT", LogicalTypeId::TINYINT}, {"SMALLINT", LogicalTypeId::SMALLINT},
            {"INTEGER", LogicalTypeId::INTEGER}, {"INT", LogicalTypeId::INTEGER}, {"BIGINT", LogicalTypeId::BIGINT},
            {"HUGEINT", LogicalTypeId::HUGEINT}, {"UTINYINT", LogicalTypeId::UTINYINT},
            {"USMALLINT", LogicalTypeId::USMALLINT}, {"UINTEGER", LogicalTypeId::UINTEGER},
            {"UBIGINT", LogicalTypeId::UBIGINT}, {"UHUGEINT", LogicalTypeId::UHUGEINT},
            {"FLOAT", LogicalTypeId::FLOAT}, {"DOUBLE", LogicalTypeId::DOUBLE}, {"DECIMAL", LogicalTypeId::DECIMAL},
            {"BOOLEAN", LogicalTypeId::BOOLEAN}, {"VARCHAR", LogicalTypeId::VARCHAR}
        };
        for(const auto& [name, id] : names){
            if(upper == name) return LogicalType(id);
        }
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
    bool IsNull() const { return isNull_; }

    template<typename T>
    T GetValue() const {
        if constexpr(std::is_same_v<T, std::string>) {
            return ToString();
        } else if constexpr(std::is_floating_point_v<T>) {
            return static_cast<T>(isInteger() ? static_cast<double>(int64_) : double_);
        } else {
            return static_cast<T>(isInteger() ? int64_ : static_cast<std::int64_t>(double_));
        }
    }
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

    // A value of a query result (duckdb-host.js): integers beyond 2^53 and the doubles JSON has no
    // literal for arrive as strings.
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
            return DOUBLE(json.is_string() ? std::strtod(json.get<std::string>().c_str(), nullptr) : json.get<double>());
        }
        Value out;
        out.type_ = type;
        if(json.is_boolean()) {
            out.int64_ = json.get<bool>() ? 1 : 0;
        } else if(json.is_string()) {
            const auto text = json.get<std::string>();
            out.int64_ = static_cast<std::int64_t>(text.front() == '-' ? std::strtoll(text.c_str(), nullptr, 10) : static_cast<std::int64_t>(std::strtoull(text.c_str(), nullptr, 10)));
        } else {
            out.int64_ = json.is_number_float() ? static_cast<std::int64_t>(json.get<double>()) : json.get<std::int64_t>();
        }
        return out;
    }

private:
    bool isInteger() const {
        const auto id = type_.id();
        return id != LogicalTypeId::DOUBLE && id != LogicalTypeId::FLOAT && id != LogicalTypeId::DECIMAL;
    }

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


struct DBConfig {
    struct Options {
        idx_t maximum_memory = 0;
        idx_t maximum_threads = 1;
    } options;
};

// An in-memory database of the engine's own, dropped with the object.
class DuckDB {
public:
    explicit DuckDB(const char* = nullptr, DBConfig* = nullptr) : id_(volt_duckdb_open()) {}
    ~DuckDB() { volt_duckdb_close(id_); }
    DuckDB(const DuckDB&) = delete;
    DuckDB& operator=(const DuckDB&) = delete;

    int id() const { return id_; }

private:
    int id_;
};

namespace detail {

// Takes the engine's reply (a JSON document it allocated) into a result.
inline std::unique_ptr<QueryResult> takeReply(char* reply) {
    auto result = std::make_unique<QueryResult>();
    const nlohmann::json document = nlohmann::json::parse(reply);
    std::free(reply);
    if(document.contains("error")) {
        result->fail(document["error"].get<std::string>());
        return result;
    }
    std::vector<std::string> names;
    std::vector<LogicalType> types;
    for(const auto& column : document["columns"]){
        names.push_back(column["name"].get<std::string>());
        types.push_back(LogicalType::fromSql(column["type"].get<std::string>()));
    }
    std::vector<std::vector<Value>> rows;
    rows.reserve(document["rows"].size());
    for(const auto& jsonRow : document["rows"]){
        std::vector<Value> row;
        row.reserve(types.size());
        for(std::size_t i = 0; i < types.size(); ++i) row.push_back(Value::fromJson(jsonRow.at(i), types[i]));
        rows.push_back(std::move(row));
    }
    result->setColumns(std::move(names), std::move(types), std::move(rows));
    return result;
}

}

class Connection {
public:
    explicit Connection(DuckDB& db) : id_(volt_duckdb_connect(db.id())) {}
    ~Connection() { volt_duckdb_disconnect(id_); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    std::unique_ptr<QueryResult> Query(const std::string& sql) {
        return detail::takeReply(volt_duckdb_query(id_, sql.c_str()));
    }

    int id() const { return id_; }

private:
    int id_;
};

// Rows for a table, sent to the engine as CSV in the table's column order when the appender closes
// (or, for a large table, as its buffer fills), as the real Appender flushes its chunks.
class Appender {
public:
    Appender(Connection& connection, std::string tableName)
        : connection_(&connection), tableName_(std::move(tableName)) {}

    ~Appender() {
        try {
            Close();
        } catch(...) {
        }
    }

    Appender(const Appender&) = delete;
    Appender& operator=(const Appender&) = delete;

    void BeginRow() { firstInRow_ = true; }

    void EndRow() {
        csv_.push_back('\n');
        if(csv_.size() >= kFlushBytes) Flush();
    }

    template<typename T>
    void Append(T value) {
        separate();
        if constexpr(std::is_same_v<T, bool>) {
            csv_ += value ? "true" : "false";
        } else if constexpr(std::is_floating_point_v<T>) {
            writeDouble(static_cast<double>(value));
        } else if constexpr(std::is_integral_v<T>) {
            csv_ += std::to_string(value);
        } else {
            writeValue(Value(value));
        }
    }

    void Append(const Value& value) {
        separate();
        writeValue(value);
    }

    void Flush() {
        if(csv_.empty()) return;
        auto result = detail::takeReply(volt_duckdb_append(connection_->id(), tableName_.c_str(), csv_.data(), csv_.size()));
        csv_.clear();
        if(result->HasError()) throw std::runtime_error("Failed to append to " + tableName_ + ": " + result->GetError());
    }

    void Close() { Flush(); }

private:
    static constexpr std::size_t kFlushBytes = 64u << 20;

    void separate() {
        if(!firstInRow_) csv_.push_back(',');
        firstInRow_ = false;
    }

    void writeDouble(double value) {
        if(std::isnan(value)) {
            csv_ += "nan";
        } else if(std::isinf(value)) {
            csv_ += value > 0 ? "inf" : "-inf";
        } else {
            char text[32];
            std::snprintf(text, sizeof(text), "%.17g", value);
            csv_ += text;
        }
    }

    // A list's literal, as DuckDB casts text to a list: [1.5, 2.0], nested as needed.
    void writeListLiteral(const Value& value) {
        csv_.push_back('[');
        bool first = true;
        for(const auto& item : value.asList()){
            if(!first) csv_ += ", ";
            first = false;
            if(item.isNull()) {
                csv_ += "NULL";
            } else if(item.type().id() == LogicalTypeId::LIST) {
                writeListLiteral(item);
            } else if(item.type().id() == LogicalTypeId::VARCHAR) {
                csv_ += '\'' + item.asString() + '\'';
            } else {
                writeScalar(item);
            }
        }
        csv_.push_back(']');
    }

    void writeScalar(const Value& value) {
        const auto id = value.type().id();
        if(id == LogicalTypeId::DOUBLE || id == LogicalTypeId::FLOAT || id == LogicalTypeId::DECIMAL) {
            writeDouble(value.asDouble());
        } else if(id == LogicalTypeId::BOOLEAN) {
            csv_ += value.asInt64() ? "true" : "false";
        } else {
            csv_ += std::to_string(value.asInt64());
        }
    }

    // A field: NULL unquoted and empty, text and lists quoted with their quotes doubled.
    void writeValue(const Value& value) {
        if(value.isNull()) return;
        const auto id = value.type().id();
        if(id != LogicalTypeId::VARCHAR && id != LogicalTypeId::LIST) {
            writeScalar(value);
            return;
        }
        csv_.push_back('"');
        const auto start = csv_.size();
        if(id == LogicalTypeId::LIST) {
            writeListLiteral(value);
        } else {
            csv_ += value.asString();
        }
        for(auto i = csv_.size(); i-- > start;){
            if(csv_[i] == '"') csv_.insert(i, 1, '"');
        }
        csv_.push_back('"');
    }

    Connection* connection_;
    std::string tableName_;
    std::string csv_;
    bool firstInRow_ = true;
};

}
