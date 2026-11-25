#pragma once

#include <volt/core/particle_property.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace Volt{

// How an analysis against a reference configuration maps the two simulation cells onto each other before it compares
// positions.
enum class AffineMapping{ NoMapping, ToReferenceCell, ToCurrentCell };

// The mapping an option names: "noMapping", "none" or "off"; "toReferenceCell" or "toReference"; "toCurrentCell" or
// "toCurrent". None for any other name.
std::optional<AffineMapping> parseAffineMapping(std::string_view name);

// The atoms of the current frame matched to the atoms of the reference frame by identifier, or by index when the
// frames carry no identifiers.
struct ReferenceMatch{
    // The index an atom with no counterpart in the other frame maps to.
    static constexpr std::size_t kNoCounterpart = std::numeric_limits<std::size_t>::max();

    // Whether an atom of the current frame may be missing from the reference.
    enum class MissingAtoms{ Allowed, Rejected };

    std::vector<std::size_t> currentToReference;
    std::vector<std::size_t> referenceToCurrent;
    bool byIdentifier = false;

    // Throws when an identifier appears twice in one frame, when a current atom is missing from the reference and
    // that is rejected, or when the frames carry no identifiers and do not hold as many atoms.
    static ReferenceMatch build(
        const Particles::ParticleProperty* identifiers,
        const Particles::ParticleProperty* referenceIdentifiers,
        std::size_t currentAtoms,
        std::size_t referenceAtoms,
        MissingAtoms missingAtoms
    );
};

}
