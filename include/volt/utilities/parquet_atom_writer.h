#pragma once

#include <volt/core/lammps_parser.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Volt {

using BucketResolver = std::function<std::string(std::size_t atomIndex)>;

class ColumnarAtomWriter {
public:
    void field(const std::string& name, double value);
    void field(const std::string& name, std::int64_t value);
    void field(const std::string& name, int value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, std::size_t value) { field(name, static_cast<std::int64_t>(value)); }
    void field(const std::string& name, bool value) { field(name, static_cast<std::int64_t>(value ? 1 : 0)); }
    void field(const std::string& name, const std::string& value);
    void field(const std::string& name, const char* value) { field(name, std::string(value)); }
    void field(const std::string& name, const std::vector<double>& values);

    struct Impl;
    explicit ColumnarAtomWriter(Impl& impl) : _impl(impl) {}

private:
    Impl& _impl;
};

using PerAtomColumnWriter = std::function<void(ColumnarAtomWriter& writer, std::size_t atomIndex)>;

using StructureIdResolver = std::function<int(std::size_t atomIndex)>;

void streamAtomsToParquet(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const PerAtomColumnWriter& writePerAtomColumns = {},
    const StructureIdResolver& resolveStructureId = {},
    bool includeStructureColumns = false
);

}
