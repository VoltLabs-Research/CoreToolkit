#include <volt/utilities/parquet_line_writer.h>
#include <volt/utilities/duckdb_parquet.h>

#include <duckdb.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Volt {

using ColType = Detail::ColType;
using DynColumn = Detail::DynColumn;

struct ColumnarLineWriter::Impl {
    std::vector<DynColumn> columns;
    std::unordered_map<std::string, std::size_t> index;
    std::int64_t rowsCompleted = 0;

    static bool isReserved(const std::string& name){
        return name == "id" || name == "points";
    }

    DynColumn& ensure(const std::string& name, ColType type){
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

    void appendDouble(const std::string& name, double v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Double);
        if(c.type == ColType::Int64) c.values.emplace_back(duckdb::Value::BIGINT(static_cast<std::int64_t>(v)));
        else                          c.values.emplace_back(duckdb::Value::DOUBLE(v));
        c.touchedThisRow = true;
    }
    void appendInt64(const std::string& name, std::int64_t v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Int64);
        if(c.type == ColType::Double) c.values.emplace_back(duckdb::Value::DOUBLE(static_cast<double>(v)));
        else                           c.values.emplace_back(duckdb::Value::BIGINT(v));
        c.touchedThisRow = true;
    }
    void appendString(const std::string& name, const std::string& v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::String);
        c.values.emplace_back(duckdb::Value(v));
        c.touchedThisRow = true;
    }
    void appendList(const std::string& name, const std::vector<double>& v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::ListDouble);
        std::vector<duckdb::Value> items;
        items.reserve(v.size());
        for(double d : v) items.emplace_back(duckdb::Value::DOUBLE(d));
        c.values.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, std::move(items)));
        c.touchedThisRow = true;
    }

    void finishRow(){
        for(auto& c : columns){
            if(!c.touchedThisRow) c.values.emplace_back(Detail::logicalTypeFor(c.type));
            c.touchedThisRow = false;
        }
        ++rowsCompleted;
    }
};

void ColumnarLineWriter::field(const std::string& name, double value){ _impl.appendDouble(name, value); }
void ColumnarLineWriter::field(const std::string& name, std::int64_t value){ _impl.appendInt64(name, value); }
void ColumnarLineWriter::field(const std::string& name, const std::string& value){ _impl.appendString(name, value); }
void ColumnarLineWriter::field(const std::string& name, const std::vector<double>& values){ _impl.appendList(name, values); }

void streamLinesToParquet(
    const std::string& filePath,
    std::size_t lineCount,
    const LinePointsResolver& resolvePoints,
    const PerLineColumnWriter& writePerLineColumns
){
    const duckdb::LogicalType pointType = duckdb::LogicalType::LIST(duckdb::LogicalType::DOUBLE);

    std::vector<duckdb::Value> pointLists;
    pointLists.reserve(lineCount);

    ColumnarLineWriter::Impl dyn;
    ColumnarLineWriter writer(dyn);

    std::vector<Point3> linePoints;
    for(std::size_t i = 0; i < lineCount; ++i){
        linePoints.clear();
        if(resolvePoints) resolvePoints(i, linePoints);

        std::vector<duckdb::Value> points;
        points.reserve(linePoints.size());
        for(const Point3& p : linePoints){
            points.emplace_back(duckdb::Value::LIST(duckdb::LogicalType::DOUBLE, {
                duckdb::Value::DOUBLE(p.x()),
                duckdb::Value::DOUBLE(p.y()),
                duckdb::Value::DOUBLE(p.z())
            }));
        }
        pointLists.emplace_back(duckdb::Value::LIST(pointType, std::move(points)));

        if(writePerLineColumns) writePerLineColumns(writer, i);
        dyn.finishRow();
    }

    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);

    std::string ddl = "CREATE TABLE lines(id UBIGINT, points DOUBLE[][]";
    for(const auto& col : dyn.columns){
        ddl += ", ";
        ddl += Detail::quoteIdent(col.name);
        ddl += ' ';
        ddl += Detail::sqlTypeFor(col.type);
    }
    ddl += ')';
    auto created = con.Query(ddl);
    if(created->HasError()){
        throw std::runtime_error(
            "Failed to stage lines for " + filePath + ": " + created->GetError()
        );
    }

    {
        duckdb::Appender appender(con, "lines");
        for(std::size_t i = 0; i < lineCount; ++i){
            appender.BeginRow();
            appender.Append<std::uint64_t>(static_cast<std::uint64_t>(i));
            appender.Append(pointLists[i]);
            for(auto& col : dyn.columns){
                appender.Append(col.values[i]);
            }
            appender.EndRow();
        }
        appender.Close();
    }

    Detail::copyTableToParquet(con, "lines", filePath);
}

}
