#pragma once

#include <volt/core/lammps_parser.h>

#include <iostream>
#include <string>
#include <filesystem>
#include <map>
#include <memory>
#include <thread>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace Volt::CLI {

using json = nlohmann::json;

inline void initLogging(const std::string& toolName = "Volt") {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    auto logger = std::make_shared<spdlog::logger>(toolName, console_sink);
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
    
    unsigned int n = std::thread::hardware_concurrency();
    if(n == 0){
        n = 1;
    }
    spdlog::info("Using {} threads (OneTBB)", n);
}

inline std::map<std::string, std::string> parseArgs(
    int argc, char* argv[],
    std::string& filename,
    std::string& outputBase
){
    std::map<std::string, std::string> options;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0) {
            const std::size_t equalsPos = arg.find('=');
            if (equalsPos != std::string::npos) {
                options[arg.substr(0, equalsPos)] = arg.substr(equalsPos + 1);
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
                options[arg] = argv[++i];
            } else {
                options[arg] = "true";
            }
        } else if (filename.empty()) {
            filename = arg;
        } else if (outputBase.empty()) {
            outputBase = arg;
        }
    }
    
    return options;
}

inline std::string deriveOutputBase(const std::string& filename, const std::string& outputBase) {
    if (!outputBase.empty()) {
        return outputBase;
    }
    std::filesystem::path inputPath(filename);
    return (inputPath.parent_path() / inputPath.stem()).string();
}

inline bool parseFrame(const std::string& filename, LammpsParser::Frame& frame) {
    spdlog::info("Parsing LAMMPS file: {}", filename);
    LammpsParser parser;
    if (!parser.parseFile(filename, frame)) {
        spdlog::error("Failed to parse LAMMPS file: {}", filename);
        return false;
    }
    spdlog::info("Successfully loaded {} atoms from the file.", frame.natoms);
    return true;
}

inline bool getBool(const std::map<std::string, std::string>& opts, const std::string& key, bool defaultVal = false) {
    auto it = opts.find(key);
    if (it == opts.end()) return defaultVal;
    return it->second == "true" || it->second == "1";
}

inline double getDouble(const std::map<std::string, std::string>& opts, const std::string& key, double defaultVal = 0.0) {
    auto it = opts.find(key);
    if (it == opts.end()) return defaultVal;
    try { return std::stod(it->second); }
    catch (...) { return defaultVal; }
}

inline int getInt(const std::map<std::string, std::string>& opts, const std::string& key, int defaultVal = 0) {
    auto it = opts.find(key);
    if (it == opts.end()) return defaultVal;
    try { return std::stoi(it->second); }
    catch (...) { return defaultVal; }
}

inline std::string getString(const std::map<std::string, std::string>& opts, const std::string& key, const std::string& defaultVal = "") {
    auto it = opts.find(key);
    return (it == opts.end()) ? defaultVal : it->second;
}

inline bool hasOption(const std::map<std::string, std::string>& opts, const std::string& key) {
    return opts.find(key) != opts.end();
}

template<typename UsageFn>
inline int handleHelpOrMissingInput(
    int argc,
    char* argv[],
    const std::map<std::string, std::string>& opts,
    const std::string& filename,
    UsageFn&& showUsage
) {
    if(argc < 2){
        showUsage(argv[0]);
        return 1;
    }
    if(hasOption(opts, "--help") || filename.empty()){
        showUsage(argv[0]);
        return filename.empty() ? 1 : 0;
    }
    return -1;
}

inline void printUsageHeader(const std::string& name, const std::string& description) {
    std::cerr << "\n" << description << "\n\n"
              << "Usage: " << name << " <lammps_file> [output_base] [options]\n\n"
              << "Arguments:\n"
              << "  <lammps_file>    Path to the LAMMPS dump file.\n"
              << "  [output_base]    Base path for output files (default: derived from input).\n\n"
              << "Options:\n";
}

inline void printHelpOption() {
    std::cerr << "  --help           Show this help message and exit.\n\n";
}

}
