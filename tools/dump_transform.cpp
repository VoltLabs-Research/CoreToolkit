#include <volt/core/lammps_parser.h>
#include <volt/core/particle_property.h>
#include <volt/analysis/expression.hpp>
#include <volt/utilities/duckdb_parquet.h>

#include <duckdb.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using Volt::LammpsParser;
using Volt::Particles::DataType;
using Volt::Particles::ParticleProperty;
using Volt::Point3;
using Volt::Vector3;

[[noreturn]] void usage(const char* program){
    std::cerr << "Usage: " << program
              << " <input.dump> <output.dump> (--slice <nx>,<ny>,<nz>,<distance>,<reverse>"
              << " | --select \"<expression>\" | --merge <props.parquet>)\n";
    std::exit(2);
}

LammpsParser::Frame compactFrame(const LammpsParser::Frame& in, const std::vector<char>& keep){
    LammpsParser::Frame out;
    out.timestep = in.timestep;
    out.simulationCell = in.simulationCell;
    out.headerOrder = in.headerOrder;
    out.headerProperties = in.headerProperties;
    out.atomColumnOrder = in.atomColumnOrder;
    out.atomColumnsScaled = in.atomColumnsScaled;

    const bool hasImages = in.hasImageFlags();
    const auto atomCount = static_cast<std::size_t>(in.natoms);

    std::size_t kept = 0;
    for(std::size_t i = 0; i < atomCount; ++i){
        if(keep[i]) ++kept;
    }

    out.positions.reserve(kept);
    out.types.reserve(kept);
    out.ids.reserve(kept);
    if(hasImages){
        out.imageX.reserve(kept);
        out.imageY.reserve(kept);
        out.imageZ.reserve(kept);
    }

    for(const auto& [name, column] : in.atomProperties){
        auto& dst = out.atomProperties[name];
        dst.dataType = column.dataType;
        switch(column.dataType){
            case DataType::Int:    dst.ints.reserve(kept); break;
            case DataType::Int64:  dst.int64s.reserve(kept); break;
            case DataType::Double: dst.doubles.reserve(kept); break;
            case DataType::Void:   break;
        }
    }

    for(std::size_t i = 0; i < atomCount; ++i){
        if(!keep[i]) continue;

        out.positions.push_back(in.positions[i]);
        out.types.push_back(i < in.types.size() ? in.types[i] : 1);
        out.ids.push_back(i < in.ids.size() ? in.ids[i] : static_cast<int>(i + 1));
        if(hasImages){
            out.imageX.push_back(in.imageX[i]);
            out.imageY.push_back(in.imageY[i]);
            out.imageZ.push_back(in.imageZ[i]);
        }

        for(const auto& [name, column] : in.atomProperties){
            auto& dst = out.atomProperties[name];
            switch(column.dataType){
                case DataType::Int:    dst.ints.push_back(column.ints[i]); break;
                case DataType::Int64:  dst.int64s.push_back(column.int64s[i]); break;
                case DataType::Double: dst.doubles.push_back(column.doubles[i]); break;
                case DataType::Void:   break;
            }
        }
    }

    out.natoms = static_cast<int>(kept);
    return out;
}

bool writeFrame(const std::string& output, const LammpsParser::Frame& frame){
    LammpsParser parser;
    return parser.writeFileMergedWithExtraColumns(output, frame, {}, {});
}

bool parseSliceArgs(const std::string& arg, Vector3& normal, double& distance, bool& reverse){
    std::array<double, 5> values{};
    std::size_t valueIndex = 0;
    std::size_t start = 0;
    while(valueIndex < values.size()){
        const std::size_t comma = arg.find(',', start);
        const std::string token = arg.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        try{
            values[valueIndex] = std::stod(token);
        }catch(const std::exception&){
            return false;
        }
        ++valueIndex;
        if(comma == std::string::npos) break;
        start = comma + 1;
    }
    if(valueIndex != values.size()) return false;

    normal = Vector3(values[0], values[1], values[2]);
    distance = values[3];
    reverse = (values[4] != 0.0);
    return true;
}

bool runSlice(const LammpsParser::Frame& frame, const std::string& arg, const std::string& output){
    Vector3 normal;
    double distance = 0.0;
    bool reverse = false;
    if(!parseSliceArgs(arg, normal, distance, reverse)){
        std::cerr << "Error: --slice expects <nx>,<ny>,<nz>,<distance>,<reverse>\n";
        return false;
    }

    const double length = normal.length();
    if(length == 0.0){
        std::cerr << "Error: --slice normal vector must be non-zero\n";
        return false;
    }
    const Vector3 unit = normal / length;

    const auto atomCount = static_cast<std::size_t>(frame.natoms);
    std::vector<char> keep(atomCount, 0);
    for(std::size_t i = 0; i < atomCount; ++i){
        const Point3& p = frame.positions[i];
        const double signedDistance =
            unit.x() * p.x() + unit.y() * p.y() + unit.z() * p.z() - distance;
        keep[i] = reverse ? (signedDistance >= 0.0) : (signedDistance <= 0.0);
    }

    const LammpsParser::Frame compacted = compactFrame(frame, keep);
    return writeFrame(output, compacted);
}

class SelectionContext{
public:
    explicit SelectionContext(const LammpsParser::Frame& frame){
        const auto atomCount = static_cast<std::size_t>(frame.natoms);

        if(!frame.positions.empty()){
            auto position = std::make_shared<ParticleProperty>();
            position->bindExternalData(
                const_cast<Point3*>(frame.positions.data()),
                atomCount, DataType::Double, 3, sizeof(Point3));
            add("Position", position);

            _x.reserve(atomCount);
            _y.reserve(atomCount);
            _z.reserve(atomCount);
            for(std::size_t i = 0; i < atomCount; ++i){
                _x.push_back(frame.positions[i].x());
                _y.push_back(frame.positions[i].y());
                _z.push_back(frame.positions[i].z());
            }
            add("x", makeScalar(_x.data(), atomCount, DataType::Double));
            add("y", makeScalar(_y.data(), atomCount, DataType::Double));
            add("z", makeScalar(_z.data(), atomCount, DataType::Double));
        }

        if(!frame.ids.empty()){
            auto ids = makeScalar(const_cast<int*>(frame.ids.data()), frame.ids.size(), DataType::Int);
            add("id", ids);
            add("ParticleIdentifier", ids);
        }
        if(!frame.types.empty()){
            auto types = makeScalar(const_cast<int*>(frame.types.data()), frame.types.size(), DataType::Int);
            add("type", types);
            add("ParticleType", types);
        }

        for(const auto& [name, column] : frame.atomProperties){
            std::shared_ptr<ParticleProperty> prop;
            switch(column.dataType){
                case DataType::Int:
                    prop = makeScalar(const_cast<int*>(column.ints.data()), column.ints.size(), DataType::Int);
                    break;
                case DataType::Int64:
                    prop = makeScalar(const_cast<std::int64_t*>(column.int64s.data()), column.int64s.size(), DataType::Int64);
                    break;
                case DataType::Double:
                    prop = makeScalar(const_cast<double*>(column.doubles.data()), column.doubles.size(), DataType::Double);
                    break;
                case DataType::Void:
                    break;
            }
            if(prop) add(name, prop);
        }

        _context.N = atomCount;
        _context.Frame = frame.timestep;
        _context.CellVolume = frame.simulationCell.volume3D();
        _context.getColumn = [this](const std::string& name) -> std::optional<Volt::Analysis::ColumnView>{
            auto it = _columns.find(name);
            if(it == _columns.end()) return std::nullopt;
            return Volt::Analysis::columnFromProperty(*it->second);
        };
    }

    const Volt::Analysis::AtomContext& context() const{ return _context; }

private:
    template<typename T>
    static std::shared_ptr<ParticleProperty> makeScalar(T* data, std::size_t count, DataType dataType){
        auto prop = std::make_shared<ParticleProperty>();
        prop->bindExternalData(data, count, dataType, 1, sizeof(T));
        return prop;
    }

    void add(const std::string& name, std::shared_ptr<ParticleProperty> prop){
        _columns[name] = std::move(prop);
    }

    std::unordered_map<std::string, std::shared_ptr<ParticleProperty>> _columns;
    std::vector<double> _x, _y, _z;
    Volt::Analysis::AtomContext _context;
};

bool runSelect(const LammpsParser::Frame& frame, const std::string& expression, const std::string& output){
    std::vector<char> keep;
    try{
        const Volt::Analysis::Expr ast = Volt::Analysis::parse(expression);
        SelectionContext ctx(frame);
        keep = Volt::Analysis::evaluateSelection(ast, ctx.context());
    }catch(const Volt::Analysis::ExpressionError& error){
        std::cerr << "Error: expression failed (line " << error.line
                  << ", column " << error.column << "): " << error.what() << '\n';
        return false;
    }

    const LammpsParser::Frame compacted = compactFrame(frame, keep);
    return writeFrame(output, compacted);
}

struct ParquetColumn{
    std::string name;
    DataType dataType = DataType::Void;
    std::vector<int> ints;
    std::vector<std::int64_t> int64s;
    std::vector<double> doubles;

    const void* data() const{
        switch(dataType){
            case DataType::Int:    return ints.data();
            case DataType::Int64:  return int64s.data();
            case DataType::Double: return doubles.data();
            case DataType::Void:   return nullptr;
        }
        return nullptr;
    }
};

DataType mapLogicalType(duckdb::LogicalTypeId id){
    switch(id){
        case duckdb::LogicalTypeId::DOUBLE:
        case duckdb::LogicalTypeId::FLOAT:
        case duckdb::LogicalTypeId::DECIMAL:
            return DataType::Double;
        case duckdb::LogicalTypeId::BIGINT:
        case duckdb::LogicalTypeId::HUGEINT:
        case duckdb::LogicalTypeId::UBIGINT:
        case duckdb::LogicalTypeId::UINTEGER:
        case duckdb::LogicalTypeId::UHUGEINT:
            return DataType::Int64;
        case duckdb::LogicalTypeId::INTEGER:
        case duckdb::LogicalTypeId::SMALLINT:
        case duckdb::LogicalTypeId::TINYINT:
        case duckdb::LogicalTypeId::USMALLINT:
        case duckdb::LogicalTypeId::UTINYINT:
        case duckdb::LogicalTypeId::BOOLEAN:
            return DataType::Int;
        default:
            return DataType::Void;
    }
}

bool runMerge(const LammpsParser::Frame& frame, const std::string& parquetPath, const std::string& output){
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);

    const std::string sql = "SELECT * FROM read_parquet(" + Volt::Detail::sqlQuote(parquetPath) + ")";
    auto result = con.Query(sql);
    if(result->HasError()){
        std::cerr << "Error: failed to read parquet: " << result->GetError() << '\n';
        return false;
    }

    const auto columnCount = result->ColumnCount();
    const auto rowCount = result->RowCount();

    int idColumn = -1;
    for(duckdb::idx_t c = 0; c < columnCount; ++c){
        if(result->names[c] == "id"){
            idColumn = static_cast<int>(c);
            break;
        }
    }
    if(idColumn < 0){
        std::cerr << "Error: parquet has no 'id' column to merge on\n";
        return false;
    }

    std::vector<int> propertyAtomIds;
    propertyAtomIds.reserve(rowCount);

    std::vector<ParquetColumn> columns;
    columns.reserve(columnCount);
    std::vector<duckdb::idx_t> columnSourceIndex;

    for(duckdb::idx_t c = 0; c < columnCount; ++c){
        if(static_cast<int>(c) == idColumn) continue;
        const DataType dataType = mapLogicalType(result->types[c].id());
        if(dataType == DataType::Void){
            std::cerr << "Warning: skipping unsupported parquet column '" << result->names[c] << "'\n";
            continue;
        }
        ParquetColumn column;
        column.name = result->names[c];
        column.dataType = dataType;
        switch(dataType){
            case DataType::Int:    column.ints.reserve(rowCount); break;
            case DataType::Int64:  column.int64s.reserve(rowCount); break;
            case DataType::Double: column.doubles.reserve(rowCount); break;
            case DataType::Void:   break;
        }
        columns.push_back(std::move(column));
        columnSourceIndex.push_back(c);
    }

    for(duckdb::idx_t row = 0; row < rowCount; ++row){
        const duckdb::Value idValue = result->GetValue(static_cast<duckdb::idx_t>(idColumn), row);
        propertyAtomIds.push_back(idValue.IsNull() ? 0 : static_cast<int>(idValue.GetValue<std::int64_t>()));

        for(std::size_t k = 0; k < columns.size(); ++k){
            const duckdb::Value value = result->GetValue(columnSourceIndex[k], row);
            ParquetColumn& column = columns[k];
            switch(column.dataType){
                case DataType::Int:
                    column.ints.push_back(value.IsNull() ? 0 : static_cast<int>(value.GetValue<std::int64_t>()));
                    break;
                case DataType::Int64:
                    column.int64s.push_back(value.IsNull() ? 0 : value.GetValue<std::int64_t>());
                    break;
                case DataType::Double:
                    column.doubles.push_back(value.IsNull() ? 0.0 : value.GetValue<double>());
                    break;
                case DataType::Void:
                    break;
            }
        }
    }

    std::vector<LammpsParser::ExtraColumn> extraColumns;
    extraColumns.reserve(columns.size());
    for(const ParquetColumn& column : columns){
        LammpsParser::ExtraColumn extra;
        extra.names = { column.name };
        extra.data = column.data();
        extra.dataType = column.dataType;
        extra.rowCount = static_cast<std::size_t>(rowCount);
        extra.componentCount = 1;
        extra.stride = 0;
        extraColumns.push_back(std::move(extra));
    }

    LammpsParser parser;
    return parser.writeFileMergedWithExtraColumns(
        output, frame, propertyAtomIds, extraColumns, /*extraHeaders*/ {}, /*overwriteExistingColumns*/ true);
}

int main(int argc, char** argv){
    if(argc < 4){
        usage(argv[0]);
    }

    const std::string input = argv[1];
    const std::string output = argv[2];
    const std::string operation = argv[3];

    LammpsParser parser;
    LammpsParser::Frame frame;
    if(!parser.parseFile(input, frame)){
        std::cerr << "Error: failed to parse dump '" << input << "'\n";
        return 1;
    }

    bool ok = false;
    if(operation == "--slice"){
        if(argc != 5) usage(argv[0]);
        ok = runSlice(frame, argv[4], output);
    }else if(operation == "--select"){
        if(argc != 5) usage(argv[0]);
        ok = runSelect(frame, argv[4], output);
    }else if(operation == "--merge"){
        if(argc != 5) usage(argv[0]);
        ok = runMerge(frame, argv[4], output);
    }else{
        std::cerr << "Error: unknown operation '" << operation << "'\n";
        usage(argv[0]);
    }

    if(!ok){
        std::cerr << "Error: " << operation << " failed\n";
        return 1;
    }
    return 0;
}
