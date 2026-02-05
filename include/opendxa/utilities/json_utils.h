#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <cstdint>
#include <ostream>
#include <algorithm>
#include <vector>

namespace OpenDXA {

using json = nlohmann::json;

/**
 * @brief Lightweight msgpack writer for JSON serialization
 */
class MsgpackWriter {
public:
    explicit MsgpackWriter(std::ostream& os) : _os(os) {}

    void write_nil();
    void write_bool(bool v);
    void write_int(int64_t v);
    void write_uint(uint64_t v);
    void write_double(double v);
    void write_str(const std::string& s);
    void write_array_header(uint32_t size);
    void write_map_header(uint32_t size);

    inline void write_key(const char* s) { write_str(std::string(s)); }
    inline void write_key(const std::string& s) { write_str(s); }

private:
    std::ostream& _os;

    void write_raw(const void* data, size_t size);
    void write_u8(uint8_t v);
    void write_u16(uint16_t v);
    void write_u32(uint32_t v);
    void write_u64(uint64_t v);
};

/**
 * @brief Simple JSON utilities for analysis packages
 * 
 * This provides basic JSON serialization functionality without
 * depending on OpenDXA-specific types like DislocationNetwork.
 */
class JsonUtils {
public:
    /**
     * @brief Write JSON data to a msgpack file
     */
    static bool writeJsonMsgpackToFile(const json& data, const std::string& filePath, bool sortKeys = true);

    /**
     * @brief Write JSON data to a regular JSON file
     */
    static bool writeJsonToFile(const json& data, const std::string& filePath, int indent = 2);

    /**
     * @brief Checked size conversion for msgpack
     */
    static inline uint32_t checked_u32_size(std::size_t n) {
        if (n > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::runtime_error("JSON container too large for msgpack u32 header.");
        }
        return static_cast<uint32_t>(n);
    }

private:
    static void writeJsonAsMsgpack(MsgpackWriter& writer, const json& data, bool sortKeys);
};

}
