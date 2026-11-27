#include <volt/utilities/columnar_row_writer.h>
#include <volt/utilities/duckdb_parquet.h>

#include <algorithm>

namespace Volt {

using Detail::ColType;
using Detail::DynColumn;

void ColumnarRowWriter::field(const std::string& name, double value){ _impl.appendDouble(name, value); }
void ColumnarRowWriter::field(const std::string& name, std::int64_t value){ _impl.appendInt64(name, value); }
void ColumnarRowWriter::field(const std::string& name, const std::string& value){ _impl.appendString(name, value); }
void ColumnarRowWriter::field(const std::string& name, const std::vector<double>& values){ _impl.appendList(name, values); }

bool ColumnarRowWriter::Impl::isReserved(const std::string& name) const{
    return std::ranges::find(reserved, name) != reserved.end();
}

DynColumn& ColumnarRowWriter::Impl::ensure(const std::string& name, ColType type){
    auto it = index.find(name);
    if(it != index.end()) return columns[it->second];

    DynColumn col;
    col.name = name;
    col.type = type;
    const duckdb::LogicalType lt = Detail::logicalTypeFor(type);
    col.values.reserve(static_cast<std::size_t>(rowsCompleted) + 1);
    for(std::int64_t r = 0; r < rowsCompleted; ++r) col.values.emplace_back(lt);

    index.emplace(name, columns.size());
    columns.push_back(std::move(col));
    return columns.back();
}

void ColumnarRowWriter::Impl::appendDouble(const std::string& name, double value){
    if(isReserved(name)) return;
    auto& c = ensure(name, ColType::Double);
    if(c.type == ColType::Int64) c.values.emplace_back(duckdb::Value::BIGINT(static_cast<std::int64_t>(value)));
    else                          c.values.emplace_back(duckdb::Value::DOUBLE(value));
    c.touchedThisRow = true;
}

void ColumnarRowWriter::Impl::appendInt64(const std::string& name, std::int64_t value){
    if(isReserved(name)) return;
    auto& c = ensure(name, ColType::Int64);
    if(c.type == ColType::Double) c.values.emplace_back(duckdb::Value::DOUBLE(static_cast<double>(value)));
    else                           c.values.emplace_back(duckdb::Value::BIGINT(value));
    c.touchedThisRow = true;
}

void ColumnarRowWriter::Impl::appendString(const std::string& name, const std::string& value){
    if(isReserved(name)) return;
    auto& c = ensure(name, ColType::String);
    c.values.emplace_back(duckdb::Value(value));
    c.touchedThisRow = true;
}

void ColumnarRowWriter::Impl::appendList(const std::string& name, const std::vector<double>& values){
    if(isReserved(name)) return;
    auto& c = ensure(name, ColType::ListDouble);
    std::vector<duckdb::Value> items;
    items.reserve(values.size());
    for(double d : values) items.emplace_back(duckdb::Value::DOUBLE(d));
    c.values.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, std::move(items)));
    c.touchedThisRow = true;
}

void ColumnarRowWriter::Impl::finishRow(){
    for(auto& c : columns){
        if(!c.touchedThisRow) c.values.emplace_back(Detail::logicalTypeFor(c.type));
        c.touchedThisRow = false;
    }
    ++rowsCompleted;
}

std::string ColumnarRowWriter::Impl::columnsDdl() const{
    std::string ddl;
    for(const auto& col : columns){
        ddl += ", ";
        ddl += Detail::quoteIdent(col.name);
        ddl += ' ';
        ddl += Detail::sqlTypeFor(col.type);
    }
    return ddl;
}

void ColumnarRowWriter::Impl::appendColumns(duckdb::Appender& appender, std::size_t row) const{
    for(const auto& col : columns){
        appender.Append(col.values[row]);
    }
}

}
