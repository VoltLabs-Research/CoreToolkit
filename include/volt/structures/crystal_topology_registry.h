#pragma once

#include <volt/core/volt.h>
#include <volt/math/point3.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Volt{

enum class CrystalSymmetryFamily{
    Unknown = 0,
    Cubic,
    Hexagonal,
    Tetragonal
};

enum class CnaLocalEnvironmentConstruction{
    None = 0,
    Direct,
    FirstShellExpansion
};

struct CnaLocalEnvironmentDescriptor{
    CnaLocalEnvironmentConstruction construction = CnaLocalEnvironmentConstruction::None;
    int expansionSeedCount = 0;
    int referenceNeighborOffset = 0;
    int referenceNeighborCount = 0;
    double cutoffMultiplier = 0.0;
    int bondStartIndex = 0;
    int extraNeighborRejectIndex = -1;
};

struct CrystalTopologySymmetry{
    Matrix3 transformation = Matrix3::Identity();
    std::vector<int> permutation;
};

struct CrystalTopologyEntry{
    std::string name;
    std::vector<std::string> nameAliases;
    std::filesystem::path topologyPath;
    std::filesystem::path poscarPath;
    std::string symmetryFamily;
    CrystalSymmetryFamily symmetryFamilyType = CrystalSymmetryFamily::Unknown;
    int structureType = 0;
    int coordinationType = 0;
    int latticeType = 0;
    int ptmMatchType = 0;
    int ptmCheckFlag = 0;
    int exportSymmetryIndex = 0;
    std::vector<int> structureTypeAliases;
    std::vector<int> coordinationTypeAliases;
    std::vector<int> latticeTypeAliases;
    int coordinationNumber = 0;
    std::vector<Vector3> latticeVectors;
    std::vector<CrystalTopologySymmetry> symmetries;
    std::vector<std::uint32_t> neighborBondRows;
    std::vector<int> cnaSignatureCodes;
    std::vector<std::array<int, 2>> commonNeighbors;
    Matrix3 primitiveCell = Matrix3::Zero();
    Matrix3 primitiveCellInverse = Matrix3::Zero();
    std::vector<Point3> basisPositions;
    std::vector<int> basisSpecies;
    CnaLocalEnvironmentDescriptor cnaLocalEnvironment;
};

class CrystalTopologyRegistry{
public:
    static const CrystalTopologyRegistry& instance();

    const CrystalTopologyEntry* findByName(std::string_view name) const;
    const CrystalTopologyEntry* findByStructureType(int structureType) const;
    const CrystalTopologyEntry* findByCoordinationType(int coordinationType) const;
    const CrystalTopologyEntry* findByLatticeType(int latticeType) const;

    const std::vector<CrystalTopologyEntry>& entries() const{
        return _entries;
    }

private:
    CrystalTopologyRegistry();

    std::vector<CrystalTopologyEntry> _entries;
    std::unordered_map<std::string, std::size_t> _nameIndex;
    std::unordered_map<int, std::size_t> _structureTypeIndex;
    std::unordered_map<int, std::size_t> _coordinationTypeIndex;
    std::unordered_map<int, std::size_t> _latticeTypeIndex;
};

const CrystalTopologyRegistry& crystalTopologyRegistry();
const CrystalTopologyEntry* crystalTopologyByName(std::string_view name);
const CrystalTopologyEntry* crystalTopologyByStructureType(int structureType);
const CrystalTopologyEntry* crystalTopologyByCoordinationType(int coordinationType);
const CrystalTopologyEntry* crystalTopologyByLatticeType(int latticeType);
CrystalSymmetryFamily crystalSymmetryFamilyByStructureType(int structureType);
std::string crystalTopologyToken(std::string_view name);

}
