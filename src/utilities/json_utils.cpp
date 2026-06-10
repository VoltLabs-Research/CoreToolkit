#include <volt/utilities/json_utils.h>
#include <volt/utilities/duckdb_parquet.h>

#include <duckdb.hpp>

#include <string>

namespace Volt {

namespace {

std::string normalizeParquetPath(const std::string& filePath){
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

} // namespace

bool JsonUtils::writeJsonToParquet(const json& data, const std::string& filePath, bool){
    const std::string outputPath = normalizeParquetPath(filePath);
    try {
        duckdb::DuckDB db(nullptr);
        duckdb::Connection con(db);

        if(con.Query("CREATE TABLE t(payload VARCHAR)")->HasError()) return false;

        {
            duckdb::Appender appender(con, "t");
            appender.BeginRow();
            appender.Append(duckdb::Value(data.dump()));
            appender.EndRow();
            appender.Close();
        }

        return Detail::copyTableToParquet(con, "t", outputPath);
    } catch (...) {
        return false;
    }
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
