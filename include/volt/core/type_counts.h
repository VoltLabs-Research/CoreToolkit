#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Volt{

// How many atoms an analysis gave each type, for the line "<title>: FCC 851234 (81.2%), Other 144914 (13.8%)": each
// named count as a share of all the atoms, in the order the line lists them.
struct TypeCounts{
    // Some of the atoms, the line's end: "; 44169 atoms in clusters (98.7%)" for {44169, "atoms in clusters"}.
    struct Share{
        std::size_t count = 0;
        std::string what;
    };

    std::string title;
    std::vector<std::pair<std::string, std::size_t>> counts;
    std::size_t atoms = 0;
    std::optional<Share> share;
};

// How many of the values have each type, keyed by type: the structure type of each atom of a classification.
template<typename Types>
std::map<int, int> countTypes(const Types& types){
    std::map<int, int> counts;
    for(const auto type : types){
        ++counts[static_cast<int>(type)];
    }
    return counts;
}

}
