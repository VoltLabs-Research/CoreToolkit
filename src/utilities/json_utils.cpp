#include <volt/utilities/json_utils.h>
#include <volt/utilities/duckdb_parquet.h>
#include <volt/utilities/parquet_writer_log.h>
#include <volt/plugin/output_files.h>

#include <duckdb.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace Volt {

std::string JsonUtils::normalizeParquetPath(const std::string& filePath){
    const std::string ext = ".parquet";
    if(filePath.size() >= ext.size() &&
       filePath.compare(filePath.size() - ext.size(), ext.size(), ext) == 0){
        return filePath;
    }
    const std::size_t dot = filePath.find_last_of('.');
    const std::size_t slash = filePath.find_last_of("/\\");
    if(dot != std::string::npos && (slash == std::string::npos || dot > slash)){
        return filePath.substr(0, dot) + ext;
    }
    return filePath + ext;
}

void JsonUtils::writeJsonToParquet(const json& data, const std::string& filePath, bool){
    ParquetWriterLog log;
    const std::string outputPath = normalizeParquetPath(filePath);
    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);

    Detail::createTable(con, "CREATE TABLE t(payload VARCHAR)", "payload", outputPath);

    std::size_t payloadBytes = 0;
    {
        std::string payload = data.dump();
        payloadBytes = payload.size();
        duckdb::Appender appender(con, "t");
        appender.BeginRow();
        appender.Append(duckdb::Value(std::move(payload)));
        appender.EndRow();
        appender.Close();
    }

    Detail::copyTableToParquet(con, "t", outputPath);

    log.dump(ParquetJsonPayload{payloadBytes, outputPath});
}

json JsonUtils::readJsonFromParquet(const std::string& filePath){
    auto db = Volt::Detail::openInMemoryDb();
    duckdb::Connection con(*db);
    auto result = con.Query("SELECT payload FROM read_parquet(" + Detail::sqlQuote(filePath) + ")");
    if(result->HasError()){
        throw std::runtime_error("DuckDB read of " + filePath + " failed: " + result->GetError());
    }
    if(result->RowCount() == 0){
        throw std::runtime_error("Parquet " + filePath + " has no rows");
    }
    return json::parse(result->GetValue(0, 0).ToString());
}

WrittenTable JsonUtils::writeSummaryTable(
    const json& summary,
    const std::string& outputBase,
    std::string_view name,
    std::string contents
){
    WrittenTable table{std::move(contents), Volt::outputPath(outputBase, name)};
    writeJsonToParquet(summary, table.path);
    return table;
}

bool JsonUtils::writeJsonToFile(const json& data, const std::string& filePath, int indent){
    try {
        std::ofstream of(filePath);
        if (!of.is_open()) return false;
        of << data.dump(indent);
        of.flush();
        return true;
    } catch (...) {
        return false;
    }
}

}
