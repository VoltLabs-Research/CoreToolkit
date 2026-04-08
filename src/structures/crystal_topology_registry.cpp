#include <volt/structures/crystal_topology_registry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace Volt{

using json = nlohmann::json;

const char* const kTopologySchema = "volt.topology.v1";
const char* const kEmbeddedTopologyBegin = "VOLT_TOPOLOGY_BEGIN";
const char* const kEmbeddedTopologyEnd = "VOLT_TOPOLOGY_END";

std::string normalizeKey(std::string value){
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character){
        if(character == '-' || character == ' '){
            return '_';
        }
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string_view text){
    const auto first = text.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos){
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

Vector3 parseVector3(const json& value){
    if(!value.is_array() || value.size() != 3){
        throw std::runtime_error("Expected a 3-component vector.");
    }
    return Vector3(
        value.at(0).get<double>(),
        value.at(1).get<double>(),
        value.at(2).get<double>()
    );
}

Point3 parsePoint3(const json& value){
    const Vector3 vector = parseVector3(value);
    return Point3(vector.x(), vector.y(), vector.z());
}

std::vector<int> parseIntegerList(const json& value){
    std::vector<int> result;
    if(!value.is_array()){
        return result;
    }
    result.reserve(value.size());
    for(const auto& item : value){
        result.push_back(item.get<int>());
    }
    return result;
}

std::vector<std::string> parseStringList(const json& value){
    std::vector<std::string> result;
    if(!value.is_array()){
        return result;
    }
    result.reserve(value.size());
    for(const auto& item : value){
        result.push_back(item.get<std::string>());
    }
    return result;
}

std::vector<Vector3> parseVectorList(const json& value){
    std::vector<Vector3> result;
    if(!value.is_array()){
        return result;
    }
    result.reserve(value.size());
    for(const auto& item : value){
        result.push_back(parseVector3(item));
    }
    return result;
}

std::vector<std::array<int, 2>> parseNeighborPairs(const json& value){
    std::vector<std::array<int, 2>> result;
    if(!value.is_array()){
        return result;
    }
    result.reserve(value.size());
    for(const auto& item : value){
        if(!item.is_array() || item.size() != 2){
            throw std::runtime_error("Expected a 2-component integer pair.");
        }
        result.push_back({item.at(0).get<int>(), item.at(1).get<int>()});
    }
    return result;
}

std::vector<CrystalTopologySymmetry> parseSymmetryPermutations(const json& value, int coordinationNumber){
    std::vector<CrystalTopologySymmetry> result;
    if(!value.is_array()){
        return result;
    }

    result.reserve(value.size());
    for(const auto& item : value){
        if(!item.is_object()){
            throw std::runtime_error("Expected symmetry_permutations entries to be objects.");
        }

        CrystalTopologySymmetry symmetry;
        symmetry.permutation.assign(static_cast<std::size_t>(coordinationNumber), -1);
        if(item.contains("transformation")){
            const auto& transformation = item.at("transformation");
            if(!transformation.is_array() || transformation.size() != 3){
                throw std::runtime_error("Expected transformation to contain three vectors.");
            }
            symmetry.transformation.column(0) = parseVector3(transformation.at(0));
            symmetry.transformation.column(1) = parseVector3(transformation.at(1));
            symmetry.transformation.column(2) = parseVector3(transformation.at(2));
        }

        const auto& permutation = item.at("permutation");
        if(!permutation.is_array() || static_cast<int>(permutation.size()) != coordinationNumber){
            throw std::runtime_error("symmetry permutation size does not match coordination_number.");
        }
        for(int index = 0; index < coordinationNumber; ++index){
            symmetry.permutation[static_cast<std::size_t>(index)] =
                permutation.at(static_cast<std::size_t>(index)).get<int>();
        }

        result.push_back(std::move(symmetry));
    }

    return result;
}

Matrix3 parsePrimitiveCell(const json& value){
    if(!value.is_array() || value.size() != 3){
        throw std::runtime_error("Expected primitive_cell with three vectors.");
    }

    Matrix3 primitiveCell = Matrix3::Zero();
    primitiveCell.column(0) = parseVector3(value.at(0));
    primitiveCell.column(1) = parseVector3(value.at(1));
    primitiveCell.column(2) = parseVector3(value.at(2));
    return primitiveCell;
}

std::vector<Point3> parseBasisPositions(const json& value){
    std::vector<Point3> result;
    if(!value.is_array()){
        return result;
    }
    result.reserve(value.size());
    for(const auto& item : value){
        result.push_back(parsePoint3(item));
    }
    return result;
}

std::vector<std::filesystem::path> topologySearchRoots(){
    std::vector<std::filesystem::path> roots;

    if(const char* envRoot = std::getenv("VOLT_TOPOLOGY_DIR")){
        if(*envRoot != '\0'){
            roots.emplace_back(envRoot);
        }
    }

#ifdef VOLT_CORETOOLKIT_SOURCE_DIR
    roots.emplace_back(std::filesystem::path(VOLT_CORETOOLKIT_SOURCE_DIR) / "topologies");
#endif

#if defined(__linux__)
    std::array<char, 4096> buffer{};
    const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if(length > 0){
        buffer[static_cast<std::size_t>(length)] = '\0';
        std::filesystem::path executablePath(buffer.data());
        std::filesystem::path current = executablePath.parent_path();
        for(int depth = 0; depth < 6 && !current.empty(); ++depth){
            roots.push_back(current / "share/volt/topologies");
            current = current.parent_path();
        }
    }
#endif

    std::vector<std::filesystem::path> filtered;
    filtered.reserve(roots.size());
    for(const auto& root : roots){
        if(root.empty()){
            continue;
        }
        const auto canonical = root.lexically_normal();
        if(std::find(filtered.begin(), filtered.end(), canonical) == filtered.end()){
            filtered.push_back(canonical);
        }
    }
    return filtered;
}

std::vector<std::filesystem::path> discoverTopologyFiles(){
    std::vector<std::filesystem::path> topologyFiles;
    std::error_code error;

    for(const auto& root : topologySearchRoots()){
        if(!std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)){
            continue;
        }

        for(const auto& entry : std::filesystem::recursive_directory_iterator(root, error)){
            if(error){
                break;
            }
            if(!entry.is_regular_file()){
                continue;
            }
            const auto extension = normalizeKey(entry.path().extension().string());
            if(extension != ".poscar" && extension != ".vasp"){
                continue;
            }
            topologyFiles.push_back(entry.path());
        }
    }

    std::sort(topologyFiles.begin(), topologyFiles.end());
    topologyFiles.erase(std::unique(topologyFiles.begin(), topologyFiles.end()), topologyFiles.end());
    return topologyFiles;
}

json parseEmbeddedTopologyDocument(const std::filesystem::path& filePath){
    std::ifstream stream(filePath);
    if(!stream){
        throw std::runtime_error("Unable to open topology POSCAR file: " + filePath.string());
    }

    std::ostringstream payload;
    std::string rawLine;
    bool insideBlock = false;
    bool foundEnd = false;

    while(std::getline(stream, rawLine)){
        std::string content = trim(rawLine);
        if(!content.empty() && content.front() == '#'){
            content = trim(std::string_view(content).substr(1));
        }

        if(!insideBlock){
            if(content == kEmbeddedTopologyBegin){
                insideBlock = true;
            }
            continue;
        }

        if(content == kEmbeddedTopologyEnd){
            foundEnd = true;
            break;
        }

        payload << content << '\n';
    }

    if(!insideBlock){
        throw std::runtime_error("Missing embedded topology metadata block in POSCAR: " + filePath.string());
    }
    if(!foundEnd){
        throw std::runtime_error("Unterminated embedded topology metadata block in POSCAR: " + filePath.string());
    }

    try{
        return json::parse(payload.str());
    }catch(const json::parse_error& error){
        throw std::runtime_error(
            "Invalid embedded topology metadata in POSCAR '" + filePath.string() + "': " + std::string(error.what())
        );
    }
}

std::vector<std::uint32_t> parseNeighborBondRows(const json& value, int coordinationNumber){
    std::vector<std::uint32_t> rows(static_cast<std::size_t>(coordinationNumber), 0u);
    if(value.is_array() && !value.empty() && value.front().is_number_unsigned()){
        if(static_cast<int>(value.size()) != coordinationNumber){
            throw std::runtime_error("neighbor_bond_rows size does not match coordination_number.");
        }
        for(int index = 0; index < coordinationNumber; ++index){
            rows[static_cast<std::size_t>(index)] = value.at(static_cast<std::size_t>(index)).get<std::uint32_t>();
        }
        return rows;
    }

    if(value.is_array()){
        for(const auto& pairValue : value){
            if(!pairValue.is_array() || pairValue.size() != 2){
                throw std::runtime_error("Expected neighbor_bond_rows to be rows or 2-column pairs.");
            }
            const int i = pairValue.at(0).get<int>();
            const int j = pairValue.at(1).get<int>();
            if(i < 0 || i >= coordinationNumber || j < 0 || j >= coordinationNumber){
                throw std::runtime_error("neighbor bond index out of range.");
            }
            rows[static_cast<std::size_t>(i)] |= (1u << j);
            rows[static_cast<std::size_t>(j)] |= (1u << i);
        }
    }
    return rows;
}

void ensureLabelAlias(std::vector<int>& aliases, int value){
    if(value <= 0){
        return;
    }
    if(std::find(aliases.begin(), aliases.end(), value) == aliases.end()){
        aliases.push_back(value);
    }
}

CrystalSymmetryFamily parseSymmetryFamily(const std::string& value){
    const std::string normalized = normalizeKey(value);
    if(normalized == "cubic"){
        return CrystalSymmetryFamily::Cubic;
    }
    if(normalized == "hexagonal"){
        return CrystalSymmetryFamily::Hexagonal;
    }
    if(normalized == "tetragonal"){
        return CrystalSymmetryFamily::Tetragonal;
    }
    return CrystalSymmetryFamily::Unknown;
}

CnaLocalEnvironmentConstruction parseCnaConstruction(const json& value){
    const std::string normalized = normalizeKey(value.get<std::string>());
    if(normalized == "direct"){
        return CnaLocalEnvironmentConstruction::Direct;
    }
    if(normalized == "first_shell_expansion"){
        return CnaLocalEnvironmentConstruction::FirstShellExpansion;
    }
    throw std::runtime_error("Unknown CNA local environment construction.");
}

CnaLocalEnvironmentDescriptor parseCnaLocalEnvironment(const json& value){
    CnaLocalEnvironmentDescriptor descriptor;
    if(!value.is_object()){
        return descriptor;
    }

    if(value.contains("construction")){
        descriptor.construction = parseCnaConstruction(value.at("construction"));
    }
    descriptor.expansionSeedCount = value.value("expansion_seed_count", 0);
    descriptor.referenceNeighborOffset = value.value("reference_neighbor_offset", 0);
    descriptor.referenceNeighborCount = value.value("reference_neighbor_count", 0);
    descriptor.cutoffMultiplier = value.value("cutoff_multiplier", 0.0);
    descriptor.bondStartIndex = value.value("bond_start_index", 0);
    descriptor.extraNeighborRejectIndex = value.value("extra_neighbor_reject_index", -1);
    return descriptor;
}

CrystalTopologyEntry parseTopologyEntry(const std::filesystem::path& filePath){
    const json document = parseEmbeddedTopologyDocument(filePath);

    if(document.value("schema", std::string()) != kTopologySchema){
        throw std::runtime_error("Unsupported embedded topology schema in: " + filePath.string());
    }

    CrystalTopologyEntry entry;
    entry.name = document.at("name").get<std::string>();
    entry.nameAliases = parseStringList(document.value("name_aliases", json::array()));
    entry.topologyPath = filePath;
    entry.poscarPath = filePath;
    entry.symmetryFamily = document.value("symmetry_family", std::string());
    entry.symmetryFamilyType = parseSymmetryFamily(entry.symmetryFamily);
    entry.coordinationNumber = document.at("coordination_number").get<int>();
    entry.latticeVectors = parseVectorList(document.at("lattice_vectors"));
    entry.primitiveCell = parsePrimitiveCell(document.at("primitive_cell"));
    if(entry.primitiveCell.determinant() <= EPSILON){
        throw std::runtime_error("primitive_cell must define a right-handed basis with positive determinant.");
    }
    entry.primitiveCellInverse = entry.primitiveCell.inverse();
    entry.ptmMatchType = document.value("ptm_match_type", 0);
    entry.ptmCheckFlag = document.value("ptm_check_flag", 0);
    entry.exportSymmetryIndex = document.value("export_symmetry_index", 0);
    entry.cnaLocalEnvironment = parseCnaLocalEnvironment(document.value("cna_local_environment", json::object()));

    const json& labels = document.at("labels");
    entry.structureType = labels.value("structure_type", 0);
    entry.coordinationType = labels.value("coordination_type", 0);
    entry.latticeType = labels.value("lattice_type", 0);
    entry.structureTypeAliases = parseIntegerList(labels.value("structure_type_aliases", json::array()));
    entry.coordinationTypeAliases = parseIntegerList(labels.value("coordination_type_aliases", json::array()));
    entry.latticeTypeAliases = parseIntegerList(labels.value("lattice_type_aliases", json::array()));
    ensureLabelAlias(entry.structureTypeAliases, entry.structureType);
    ensureLabelAlias(entry.coordinationTypeAliases, entry.coordinationType);
    ensureLabelAlias(entry.latticeTypeAliases, entry.latticeType);

    entry.symmetries = parseSymmetryPermutations(document.value("symmetry_permutations", json::array()), entry.coordinationNumber);
    entry.neighborBondRows = parseNeighborBondRows(document.at("neighbor_bond_rows"), entry.coordinationNumber);
    entry.cnaSignatureCodes = parseIntegerList(document.value("cna_signature_codes", json::array()));
    entry.commonNeighbors = parseNeighborPairs(document.value("common_neighbors", json::array()));

    if(document.contains("basis_positions")){
        entry.basisPositions = parseBasisPositions(document.at("basis_positions"));
    }
    if(document.contains("basis_species")){
        entry.basisSpecies = parseIntegerList(document.at("basis_species"));
    }

    if(entry.coordinationNumber <= 0){
        throw std::runtime_error("coordination_number must be positive.");
    }
    if(static_cast<int>(entry.latticeVectors.size()) < entry.coordinationNumber){
        throw std::runtime_error("lattice_vectors count is smaller than coordination_number.");
    }
    if(static_cast<int>(entry.neighborBondRows.size()) != entry.coordinationNumber){
        throw std::runtime_error("neighbor_bond_rows count is smaller than coordination_number.");
    }
    if(!entry.cnaSignatureCodes.empty() &&
       static_cast<int>(entry.cnaSignatureCodes.size()) != entry.coordinationNumber){
        throw std::runtime_error("cna_signature_codes size does not match coordination_number.");
    }
    if(static_cast<int>(entry.commonNeighbors.size()) != entry.coordinationNumber){
        throw std::runtime_error("common_neighbors size does not match coordination_number.");
    }
    if(entry.symmetries.empty()){
        throw std::runtime_error("symmetry_permutations must be explicitly defined.");
    }

    return entry;
}

template<typename Map>
void registerAliases(Map& indexMap, const std::vector<int>& aliases, std::size_t index, const char* labelName){
    for(const int alias : aliases){
        if(alias <= 0){
            continue;
        }
        const auto [it, inserted] = indexMap.emplace(alias, index);
        if(!inserted && it->second != index){
            throw std::runtime_error(std::string("Duplicate topology alias for ") + labelName + ": " + std::to_string(alias));
        }
    }
}

void registerTopologyName(std::unordered_map<std::string, std::size_t>& nameIndex, const std::string& name, std::size_t index){
    const auto normalized = normalizeKey(name);
    const auto [it, inserted] = nameIndex.emplace(normalized, index);
    if(!inserted && it->second != index){
        throw std::runtime_error("Duplicate topology name alias: " + name);
    }
}

std::string uppercaseToken(std::string_view name){
    std::string token(name);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char character){
        if(character == '-' || character == ' '){
            return '_';
        }
        return static_cast<char>(std::toupper(character));
    });
    return token;
}

bool almostEqual(double lhs, double rhs){
    return std::abs(lhs - rhs) <= 1e-12;
}

bool equivalentVector3(const Vector3& lhs, const Vector3& rhs){
    return almostEqual(lhs.x(), rhs.x())
        && almostEqual(lhs.y(), rhs.y())
        && almostEqual(lhs.z(), rhs.z());
}

bool equivalentPoint3(const Point3& lhs, const Point3& rhs){
    return almostEqual(lhs.x(), rhs.x())
        && almostEqual(lhs.y(), rhs.y())
        && almostEqual(lhs.z(), rhs.z());
}

bool equivalentMatrix3(const Matrix3& lhs, const Matrix3& rhs){
    for(int row = 0; row < 3; ++row){
        for(int column = 0; column < 3; ++column){
            if(!almostEqual(lhs(row, column), rhs(row, column))){
                return false;
            }
        }
    }
    return true;
}

bool equivalentTopologyEntries(const CrystalTopologyEntry& lhs, const CrystalTopologyEntry& rhs){
    if(lhs.name != rhs.name ||
       lhs.nameAliases != rhs.nameAliases ||
       lhs.symmetryFamily != rhs.symmetryFamily ||
       lhs.structureType != rhs.structureType ||
       lhs.coordinationType != rhs.coordinationType ||
       lhs.latticeType != rhs.latticeType ||
       lhs.ptmMatchType != rhs.ptmMatchType ||
       lhs.ptmCheckFlag != rhs.ptmCheckFlag ||
       lhs.structureTypeAliases != rhs.structureTypeAliases ||
       lhs.coordinationTypeAliases != rhs.coordinationTypeAliases ||
       lhs.latticeTypeAliases != rhs.latticeTypeAliases ||
       lhs.coordinationNumber != rhs.coordinationNumber ||
       lhs.symmetries.size() != rhs.symmetries.size() ||
       lhs.neighborBondRows != rhs.neighborBondRows ||
       lhs.cnaSignatureCodes != rhs.cnaSignatureCodes ||
       lhs.commonNeighbors != rhs.commonNeighbors ||
       lhs.basisSpecies != rhs.basisSpecies ||
       lhs.cnaLocalEnvironment.construction != rhs.cnaLocalEnvironment.construction ||
       lhs.cnaLocalEnvironment.expansionSeedCount != rhs.cnaLocalEnvironment.expansionSeedCount ||
       lhs.cnaLocalEnvironment.referenceNeighborOffset != rhs.cnaLocalEnvironment.referenceNeighborOffset ||
       lhs.cnaLocalEnvironment.referenceNeighborCount != rhs.cnaLocalEnvironment.referenceNeighborCount ||
       !almostEqual(lhs.cnaLocalEnvironment.cutoffMultiplier, rhs.cnaLocalEnvironment.cutoffMultiplier) ||
       lhs.cnaLocalEnvironment.bondStartIndex != rhs.cnaLocalEnvironment.bondStartIndex ||
       lhs.cnaLocalEnvironment.extraNeighborRejectIndex != rhs.cnaLocalEnvironment.extraNeighborRejectIndex ||
       lhs.latticeVectors.size() != rhs.latticeVectors.size() ||
       lhs.basisPositions.size() != rhs.basisPositions.size() ||
       !equivalentMatrix3(lhs.primitiveCell, rhs.primitiveCell) ||
       !equivalentMatrix3(lhs.primitiveCellInverse, rhs.primitiveCellInverse)){
        return false;
    }

    for(std::size_t index = 0; index < lhs.latticeVectors.size(); ++index){
        if(!equivalentVector3(lhs.latticeVectors[index], rhs.latticeVectors[index])){
            return false;
        }
    }
    for(std::size_t index = 0; index < lhs.symmetries.size(); ++index){
        if(!equivalentMatrix3(lhs.symmetries[index].transformation, rhs.symmetries[index].transformation) ||
           lhs.symmetries[index].permutation != rhs.symmetries[index].permutation){
            return false;
        }
    }
    for(std::size_t index = 0; index < lhs.basisPositions.size(); ++index){
        if(!equivalentPoint3(lhs.basisPositions[index], rhs.basisPositions[index])){
            return false;
        }
    }

    return true;
}

CrystalTopologyRegistry::CrystalTopologyRegistry(){
    const auto topologyFiles = discoverTopologyFiles();
    if(topologyFiles.empty()){
        throw std::runtime_error("No topology POSCAR files with embedded metadata were found.");
    }

    _entries.reserve(topologyFiles.size());
    std::unordered_map<std::string, std::size_t> existingByName;
    for(const auto& filePath : topologyFiles){
        CrystalTopologyEntry entry = parseTopologyEntry(filePath);
        const std::string normalizedName = normalizeKey(entry.name);
        const auto existing = existingByName.find(normalizedName);
        if(existing == existingByName.end()){
            existingByName.emplace(normalizedName, _entries.size());
            _entries.push_back(std::move(entry));
            continue;
        }

        if(!equivalentTopologyEntries(_entries[existing->second], entry)){
            throw std::runtime_error(
                "Conflicting topology definitions found for lattice '" + entry.name + "'."
            );
        }
    }

    for(std::size_t index = 0; index < _entries.size(); ++index){
        const auto& entry = _entries[index];
        registerTopologyName(_nameIndex, entry.name, index);
        registerTopologyName(_nameIndex, uppercaseToken(entry.name), index);
        for(const auto& alias : entry.nameAliases){
            registerTopologyName(_nameIndex, alias, index);
            registerTopologyName(_nameIndex, uppercaseToken(alias), index);
        }
        registerAliases(_structureTypeIndex, entry.structureTypeAliases, index, "structure_type");
        registerAliases(_coordinationTypeIndex, entry.coordinationTypeAliases, index, "coordination_type");
        registerAliases(_latticeTypeIndex, entry.latticeTypeAliases, index, "lattice_type");
    }
}

const CrystalTopologyRegistry& CrystalTopologyRegistry::instance(){
    static const CrystalTopologyRegistry registry;
    return registry;
}

const CrystalTopologyEntry* CrystalTopologyRegistry::findByName(std::string_view name) const{
    const auto it = _nameIndex.find(normalizeKey(std::string(name)));
    return it == _nameIndex.end() ? nullptr : &_entries[it->second];
}

const CrystalTopologyEntry* CrystalTopologyRegistry::findByStructureType(int structureType) const{
    const auto it = _structureTypeIndex.find(structureType);
    return it == _structureTypeIndex.end() ? nullptr : &_entries[it->second];
}

const CrystalTopologyEntry* CrystalTopologyRegistry::findByCoordinationType(int coordinationType) const{
    const auto it = _coordinationTypeIndex.find(coordinationType);
    return it == _coordinationTypeIndex.end() ? nullptr : &_entries[it->second];
}

const CrystalTopologyEntry* CrystalTopologyRegistry::findByLatticeType(int latticeType) const{
    const auto it = _latticeTypeIndex.find(latticeType);
    return it == _latticeTypeIndex.end() ? nullptr : &_entries[it->second];
}

const CrystalTopologyRegistry& crystalTopologyRegistry(){
    return CrystalTopologyRegistry::instance();
}

const CrystalTopologyEntry* crystalTopologyByName(std::string_view name){
    return crystalTopologyRegistry().findByName(name);
}

const CrystalTopologyEntry* crystalTopologyByStructureType(int structureType){
    return crystalTopologyRegistry().findByStructureType(structureType);
}

const CrystalTopologyEntry* crystalTopologyByCoordinationType(int coordinationType){
    return crystalTopologyRegistry().findByCoordinationType(coordinationType);
}

const CrystalTopologyEntry* crystalTopologyByLatticeType(int latticeType){
    return crystalTopologyRegistry().findByLatticeType(latticeType);
}

CrystalSymmetryFamily crystalSymmetryFamilyByStructureType(int structureType){
    const auto* entry = crystalTopologyByStructureType(structureType);
    return entry ? entry->symmetryFamilyType : CrystalSymmetryFamily::Unknown;
}

std::string crystalTopologyToken(std::string_view name){
    return uppercaseToken(name);
}

}
