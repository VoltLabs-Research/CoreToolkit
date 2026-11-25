#pragma once

#include <volt/core/lammps_parser.h>
#include <volt/core/run_log.h>

#include <cstddef>
#include <ios>

namespace Volt{

// The log of the LAMMPS reader and writer: each read or write makes one and hands it what it decoded or wrote.
class LammpsParserLog : public RunLog{
public:
    // A frame as the reader decoded it: what the reader decided that the caller's load and cell lines do not show.
    struct DecodedFrame{
        const char* format;
        bool triclinic;
        const LammpsParser::Frame& frame;
    };

    // A dump written, with its size as the stream counted it (negative when the stream could not say).
    struct WrittenDump{
        std::size_t atoms;
        std::size_t columns;
        std::streamoff bytes;
    };

    using RunLog::dump;

    void dump(const DecodedFrame& decoded);
    void dump(const WrittenDump& written);
};

}
