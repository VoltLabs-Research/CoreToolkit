#include <volt/utilities/json_utils.h>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

#include <memory>
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
        arrow::StringBuilder payloadBuilder;
        if(!payloadBuilder.Append(data.dump()).ok()) return false;

        std::shared_ptr<arrow::Array> payloadArray;
        if(!payloadBuilder.Finish(&payloadArray).ok()) return false;

        auto schema = arrow::schema({arrow::field("payload", arrow::utf8())});
        auto table = arrow::Table::Make(schema, {payloadArray});

        auto outFileResult = arrow::io::FileOutputStream::Open(outputPath);
        if(!outFileResult.ok()) return false;
        std::shared_ptr<arrow::io::FileOutputStream> outFile = *outFileResult;

        parquet::WriterProperties::Builder props;
        props.compression(parquet::Compression::ZSTD);

        const arrow::Status status = parquet::arrow::WriteTable(
            *table, arrow::default_memory_pool(), outFile, /*chunk_size=*/1, props.build());
        return status.ok();
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
