#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <cstdint>

namespace Volt {

using json = nlohmann::json;

class JsonUtils {
public:
    static void writeJsonToParquet(const json& data, const std::string& filePath, bool = false);

    static bool writeJsonToFile(const json& data, const std::string& filePath, int indent = 2);
};

}
