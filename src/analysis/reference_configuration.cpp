#include <volt/analysis/reference_configuration.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Volt{

using namespace Volt::Particles;

std::optional<AffineMapping> parseAffineMapping(std::string_view name){
    if(name == "noMapping" || name == "none" || name == "off") return AffineMapping::NoMapping;
    if(name == "toReferenceCell" || name == "toReference") return AffineMapping::ToReferenceCell;
    if(name == "toCurrentCell" || name == "toCurrent") return AffineMapping::ToCurrentCell;
    return std::nullopt;
}

ReferenceMatch ReferenceMatch::build(
    const ParticleProperty* identifiers,
    const ParticleProperty* referenceIdentifiers,
    std::size_t currentAtoms,
    std::size_t referenceAtoms,
    MissingAtoms missingAtoms
){
    ReferenceMatch match;
    match.currentToReference.resize(currentAtoms);
    match.referenceToCurrent.resize(referenceAtoms);

    if(!identifiers || !referenceIdentifiers){
        if(currentAtoms != referenceAtoms){
            throw std::runtime_error("Cannot calculate displacements. Numbers of particles in reference configuration "
                "and current configuration do not match.");
        }
        for(std::size_t i = 0; i < currentAtoms; i++){
            match.currentToReference[i] = i;
            match.referenceToCurrent[i] = i;
        }
        return match;
    }

    if(identifiers->size() != currentAtoms){
        throw std::runtime_error("Particle identifiers do not cover every atom of the current configuration.");
    }
    if(referenceIdentifiers->size() != referenceAtoms){
        throw std::runtime_error("Particle identifiers do not cover every atom of the reference configuration.");
    }

    std::unordered_map<int, std::size_t> referenceIndex;
    referenceIndex.reserve(referenceAtoms * 2);
    for(std::size_t i = 0; i < referenceAtoms; i++){
        if(!referenceIndex.emplace(referenceIdentifiers->getInt(i), i).second){
            throw std::runtime_error("Particles with duplicate identifiers detected in reference configuration.");
        }
    }

    std::unordered_map<int, std::size_t> currentIndex;
    currentIndex.reserve(currentAtoms * 2);
    for(std::size_t i = 0; i < currentAtoms; i++){
        if(!currentIndex.emplace(identifiers->getInt(i), i).second){
            throw std::runtime_error("Particles with duplicate identifiers detected in current configuration.");
        }
    }

    for(std::size_t i = 0; i < currentAtoms; i++){
        const int id = identifiers->getInt(i);
        const auto it = referenceIndex.find(id);
        if(it != referenceIndex.end()){
            match.currentToReference[i] = it->second;
        }else if(missingAtoms == MissingAtoms::Rejected){
            throw std::runtime_error("Atom id " + std::to_string(id) + " of the current frame is not in the reference");
        }else{
            match.currentToReference[i] = kNoCounterpart;
        }
    }

    for(std::size_t i = 0; i < referenceAtoms; i++){
        const auto it = currentIndex.find(referenceIdentifiers->getInt(i));
        match.referenceToCurrent[i] = it != currentIndex.end() ? it->second : kNoCounterpart;
    }

    match.byIdentifier = true;
    return match;
}

}
