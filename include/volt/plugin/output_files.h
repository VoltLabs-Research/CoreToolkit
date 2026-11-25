#pragma once

#include <volt/core/progress.h>

#include <string>
#include <string_view>
#include <vector>

namespace Volt{

// The file a plugin writes as `name`: next to the output base, as <base>_<name>.parquet.
inline std::string outputPath(const std::string& base, std::string_view name){
    return base + "_" + std::string(name) + ".parquet";
}

// One file a plugin may write: what the job shows while it is written, its share of the writing time, the flag of
// the plugin's exports that asks for it (none: always written), and how to write it from what the run built.
template<typename Exports, typename Outputs>
struct OutputFile{
    const char* text;
    double weight;
    bool Exports::* requested;
    void (*write)(const Outputs& outputs);

    bool isRequested(const Exports& exports) const{ return requested == nullptr || exports.*requested; }
};

// The progress steps of writing these files, for the step that writes them (see totalWeight).
template<typename Files, typename Exports>
std::vector<ProgressStep> outputSteps(const Files& files, const Exports& exports){
    std::vector<ProgressStep> steps;
    for(const auto& file : files){
        steps.push_back({file.text, file.weight, file.isRequested(exports)});
    }
    return steps;
}

// Writes each file the exports ask for, in order, one progress step per file.
template<typename Files, typename Exports, typename Outputs>
void writeOutputFiles(const Files& files, const Exports& exports, const Outputs& outputs){
    ProgressSteps steps(outputSteps(files, exports));
    for(const auto& file : files){
        if(file.isRequested(exports)) file.write(outputs);
        steps.next();
    }
}

}
