#pragma once

#include <volt/utilities/written_table.h>

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <fstream>
#include <cstdint>

namespace Volt {

using json = nlohmann::json;

class JsonUtils {
public:
    static void writeJsonToParquet(const json& data, const std::string& filePath, bool = false);
    // The document a parquet file of writeJsonToParquet holds: its first payload row. Throws when it cannot be read.
    static json readJsonFromParquet(const std::string& filePath);

    // Writes the summary of a run next to the output base, as <base>_<name>.parquet; its line names it as contents.
    static WrittenTable writeSummaryTable(
        const json& summary,
        const std::string& outputBase,
        std::string_view name,
        std::string contents = "Summary"
    );

    static bool writeJsonToFile(const json& data, const std::string& filePath, int indent = 2);

private:
    // The path with a .parquet extension in place of the one it has, if any.
    static std::string normalizeParquetPath(const std::string& filePath);
};

}
