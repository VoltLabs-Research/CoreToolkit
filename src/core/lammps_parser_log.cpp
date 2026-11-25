#include <volt/core/lammps_parser_log.h>
#include <volt/utilities/byte_size.h>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <string>

namespace Volt{

void LammpsParserLog::dump(const DecodedFrame& decoded){
    const LammpsParser::Frame& frame = decoded.frame;
    spdlog::info("  Format: {}, {} cell, {}",
        decoded.format ? decoded.format : "unknown",
        decoded.triclinic ? "triclinic" : "orthogonal",
        frame.atomColumnsScaled ? "scaled coordinates converted to Cartesian" : "Cartesian coordinates");
    if(frame.natoms > 0 && frame.ids.empty()){
        spdlog::warn("  No atom id column: atoms are numbered by their order in the file");
    }
}

void LammpsParserLog::dump(const WrittenDump& written){
    spdlog::info("  Wrote LAMMPS dump: {} atoms x {} columns ({}) in {}", written.atoms, written.columns,
        written.bytes >= 0 ? Detail::formatByteSize(static_cast<std::uintmax_t>(written.bytes)) : std::string("size unknown"),
        runElapsed());
}

}
