#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace Volt::Detail {

// A byte count as the execution log shows it: "812 B", "3.4 KiB", "21.7 MiB", "1.2 GiB".
inline std::string formatByteSize(std::uintmax_t bytes){
    static constexpr const char* kUnits[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while(value >= 1024.0 && unit + 1 < sizeof(kUnits) / sizeof(kUnits[0])){
        value /= 1024.0;
        ++unit;
    }
    char text[32];
    std::snprintf(text, sizeof(text), unit == 0 ? "%.0f %s" : "%.1f %s", value, kUnits[unit]);
    return text;
}

// Size of a file that was just written, for its log line: one stat, never a throw.
inline std::string writtenFileSize(const std::string& path){
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    return error ? std::string("size unknown") : formatByteSize(bytes);
}

}
