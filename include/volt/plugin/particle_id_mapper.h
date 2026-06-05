#pragma once

#include <volt/core/particle_property.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <numeric>
#include <vector>

namespace Volt::Plugin {

struct ParticleIdMapping {
    std::vector<std::size_t> currentToRef;
    std::vector<std::size_t> refToCurrent;

    static constexpr std::size_t UNMAPPED = std::numeric_limits<std::size_t>::max();
};

struct ParticleIdMapperOptions {
    bool requireCompleteCurrentToRef = false;
    bool requireCompleteRefToCurrent = false;
};

inline ParticleIdMapping buildParticleIdMapping(
    Particles::ParticleProperty* currentIds,
    Particles::ParticleProperty* refIds,
    std::size_t currentCount,
    std::size_t refCount,
    const ParticleIdMapperOptions& opts = {}
) {
    ParticleIdMapping result;
    result.currentToRef.resize(currentCount);
    result.refToCurrent.resize(refCount);

    if (!currentIds || !refIds) {
        if (currentCount != refCount)
            throw std::runtime_error(
                "Cannot map particles: counts differ and no identifiers provided.");
        std::iota(result.currentToRef.begin(), result.currentToRef.end(), std::size_t{0});
        std::iota(result.refToCurrent.begin(), result.refToCurrent.end(), std::size_t{0});
        return result;
    }

    std::unordered_map<int, std::size_t> refMap;
    refMap.reserve(refCount * 2);
    for (std::size_t i = 0; i < refCount; ++i) {
        const int id = refIds->getInt(i);
        auto [it, inserted] = refMap.emplace(id, i);
        if (!inserted)
            throw std::runtime_error(
                "Duplicate particle identifier in reference configuration.");
    }

    std::unordered_map<int, std::size_t> currMap;
    currMap.reserve(currentCount * 2);
    for (std::size_t i = 0; i < currentCount; ++i) {
        const int id = currentIds->getInt(i);
        auto [it, inserted] = currMap.emplace(id, i);
        if (!inserted)
            throw std::runtime_error(
                "Duplicate particle identifier in current configuration.");
    }

    for (std::size_t i = 0; i < currentCount; ++i) {
        const int id = currentIds->getInt(i);
        auto it = refMap.find(id);
        if (it != refMap.end()) {
            result.currentToRef[i] = it->second;
        } else if (opts.requireCompleteCurrentToRef) {
            throw std::runtime_error(
                "Particle ID in current config not found in reference.");
        } else {
            result.currentToRef[i] = ParticleIdMapping::UNMAPPED;
        }
    }

    for (std::size_t i = 0; i < refCount; ++i) {
        const int id = refIds->getInt(i);
        auto it = currMap.find(id);
        if (it != currMap.end()) {
            result.refToCurrent[i] = it->second;
        } else if (opts.requireCompleteRefToCurrent) {
            throw std::runtime_error(
                "Particle ID in reference config not found in current.");
        } else {
            result.refToCurrent[i] = ParticleIdMapping::UNMAPPED;
        }
    }

    return result;
}

} // namespace Volt::Plugin
