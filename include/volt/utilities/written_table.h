#pragma once

#include <string>

namespace Volt{

// A table a run wrote, named in its log line the way scripts look for it: "<contents> parquet written to <path>".
// A table that could not be written says why in error, and its line is a warning.
struct WrittenTable{
    std::string contents;
    std::string path;
    std::string error;
};

// The per-atom table of a run, <base>_atoms.parquet, whose line is "Exported atoms data to: <path>".
struct AtomsTable{
    std::string path;
};

}
