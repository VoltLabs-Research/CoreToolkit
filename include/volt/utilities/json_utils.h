#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <cstdint>

namespace Volt {

using json = nlohmann::json;

/**
 * @brief JSON utilities for analysis packages.
 *
 * Summary/aggregate plugin results (dislocation networks, meshes, charts,
 * cluster stats) are arbitrary JSON structures that are not naturally tabular.
 * They are persisted losslessly as a single-row Parquet file carrying the JSON
 * document in a `payload` string column, so the whole ecosystem stays on one
 * storage format (Parquet).
 */
class JsonUtils {
public:
    /**
     * @brief Write a JSON document as a single-row Parquet file.
     *
     * Schema: one column `payload` (UTF-8 string) holding `data.dump()`.
     * The path extension is normalized to `.parquet`.
     *
     * The trailing boolean is accepted (and ignored) so call sites that used the
     * previous serializer signature port over with a pure rename; key ordering
     * is irrelevant for a single opaque JSON payload.
     */
    static bool writeJsonToParquet(const json& data, const std::string& filePath, bool = false);

    /**
     * @brief Write JSON data to a regular JSON file (debug/inspection only).
     */
    static bool writeJsonToFile(const json& data, const std::string& filePath, int indent = 2);
};

}
