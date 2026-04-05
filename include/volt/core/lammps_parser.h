#pragma once

#include <volt/core/particle_property.h>
#include <volt/core/volt.h>
#include <volt/core/simulation_cell.h>
#include <volt/math/lin_alg.h>
#include <cstdint>
#include <fstream>
#include <unordered_map>

namespace Volt{

class LammpsParser{
public:
    LammpsParser(){}

    struct AtomColumn{
        DataType dataType = DataType::Void;
        std::vector<int> ints;
        std::vector<std::int64_t> int64s;
        std::vector<double> doubles;

        std::size_t size() const noexcept{
            switch(dataType){
                case DataType::Int:
                    return ints.size();
                case DataType::Int64:
                    return int64s.size();
                case DataType::Double:
                    return doubles.size();
                case DataType::Void:
                    return 0;
            }
            return 0;
        }

        const void* constData() const noexcept{
            switch(dataType){
                case DataType::Int:
                    return ints.data();
                case DataType::Int64:
                    return int64s.data();
                case DataType::Double:
                    return doubles.data();
                case DataType::Void:
                    return nullptr;
            }
            return nullptr;
        }

        void* data() noexcept{
            switch(dataType){
                case DataType::Int:
                    return ints.data();
                case DataType::Int64:
                    return int64s.data();
                case DataType::Double:
                    return doubles.data();
                case DataType::Void:
                    return nullptr;
            }
            return nullptr;
        }
    };

    struct Frame{
        int timestep;
        int natoms;
        SimulationCell simulationCell;
        std::vector<Point3> positions;
        std::vector<int> types;
        std::vector<int> ids;
        std::vector<std::string> headerOrder;
        std::unordered_map<std::string, std::string> headerProperties;
        std::vector<std::string> atomColumnOrder;
        bool atomColumnsScaled = false;
        std::unordered_map<std::string, AtomColumn> atomProperties;

        const std::string* findHeaderProperty(const std::string& name) const{
            auto it = headerProperties.find(name);
            return it == headerProperties.end() ? nullptr : &it->second;
        }

        std::string* findHeaderProperty(const std::string& name){
            auto it = headerProperties.find(name);
            return it == headerProperties.end() ? nullptr : &it->second;
        }

        const AtomColumn* findAtomProperty(const std::string& name) const{
            auto it = atomProperties.find(name);
            return it == atomProperties.end() ? nullptr : &it->second;
        }

        AtomColumn* findAtomProperty(const std::string& name){
            auto it = atomProperties.find(name);
            return it == atomProperties.end() ? nullptr : &it->second;
        }
    };

    struct ExtraColumn{
        std::vector<std::string> names;
        const void* data = nullptr;
        DataType dataType = DataType::Void;
        std::size_t rowCount = 0;
        std::size_t componentCount = 1;
        std::size_t stride = 0;
    };

    struct ExtraHeader{
        std::string name;
        std::string value;
    };

    static void writeColumn(
        std::vector<LammpsParser::ExtraColumn>& columns,
        std::vector<std::string> names, 
        ParticleProperty* property
    ){
        columns.push_back({
            names,
            property->constData(),
            property->dataType(),
            property->size(),
            property->componentCount(),
            property->stride()
        });
    }

    static void writeColumn(
        std::vector<LammpsParser::ExtraColumn>& columns,
        const std::vector<std::string>& names,
        const std::shared_ptr<ParticleProperty>& property
    ){
        if(!property) return;

        writeColumn(columns, names, property.get());
    }

    bool parseFile(const std::string &filename, Frame &frame);
    bool writeFile(const std::string& filename, const Frame& frame);
    bool writeFileWithExtraColumns(
        const std::string& filename,
        const Frame& frame,
        const std::vector<int>& propertyAtomIds,
        const std::vector<ExtraColumn>& extraColumns,
        const std::vector<ExtraHeader>& extraHeaders = {}
    );
    bool writeFileMergedWithExtraColumns(
        const std::string& filename,
        const Frame& frame,
        const std::vector<int>& propertyAtomIds,
        const std::vector<ExtraColumn>& extraColumns,
        const std::vector<ExtraHeader>& extraHeaders = {},
        bool overwriteExistingColumns = true
    );

private:
    bool parseStream(std::istream &in, Frame &frame);
    bool readHeader(std::istream &in, Frame &f);
    bool readBoxBounds(std::istream &in, Frame &f);
    bool readAtomData(std::istream &in, Frame &f);

    std::vector<std::string> parseColumns(const std::string &line);

    int findColumn(const std::vector<std::string> &cols, const std::string &name);
};

}
