#include <volt/core/lammps_parser.h>
#include <lammpsio/reader_registry.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <iomanip>
#include <cstring>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <fast_float/fast_float.h>

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Volt{

namespace LammpsParserDetail {

struct LineView{
    const char* begin = nullptr;
    const char* end = nullptr;
};

inline bool readLine(const char*& cursor, const char* end, LineView& line){
    if(cursor >= end) return false;
    const char* lineEnd = static_cast<const char*>(std::memchr(cursor, '\n', end - cursor));
    if(lineEnd){
        line.begin = cursor;
        line.end = lineEnd;
        cursor = lineEnd + 1;
    }else{
        line.begin = cursor;
        line.end = end;
        cursor = end;
    }
    if(line.end > line.begin && *(line.end - 1) == '\r'){
        --line.end;
    }
    return true;
}

inline bool lineStartsWith(const LineView& line, const char* prefix){
    size_t len = std::strlen(prefix);
    return (static_cast<size_t>(line.end - line.begin) >= len) &&
           (std::memcmp(line.begin, prefix, len) == 0);
}

inline std::string trimCopy(std::string_view text){
    std::size_t begin = 0;
    std::size_t end = text.size();

    while(begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')){
        ++begin;
    }
    while(end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')){
        --end;
    }

    return std::string(text.substr(begin, end - begin));
}

inline std::string lineToString(const LineView& line){
    return std::string(line.begin, static_cast<std::size_t>(line.end - line.begin));
}

inline std::string toLowerCopy(std::string text){
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

inline std::string stripCommentCopy(std::string_view text){
    const std::size_t comment = text.find('#');
    if(comment != std::string_view::npos){
        text = text.substr(0, comment);
    }
    return trimCopy(text);
}

inline const char* skipSpaces(const char* p, const char* end){
    while(p < end && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

inline const char* skipToken(const char* p, const char* end){
    while(p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') ++p;
    return p;
}

inline bool parseIntToken(const char* start, const char* end, int& out){
    if(start >= end) return false;
    int sign = 1;
    if(*start == '-'){
        sign = -1;
        ++start;
    }
    int value = 0;
    bool any = false;
    for(const char* p = start; p < end; ++p){
        char c = *p;
        if(c < '0' || c > '9') break;
        value = value * 10 + (c - '0');
        any = true;
    }
    if(!any) return false;
    out = value * sign;
    return true;
}

inline bool parseDoubleToken(const char* start, const char* end, double& out){
    auto result = fast_float::from_chars(start, end, out);
    return result.ec == std::errc();
}

inline bool parseIntLine(const LineView& line, int& out){
    const char* p = skipSpaces(line.begin, line.end);
    return parseIntToken(p, line.end, out);
}

inline bool nextDoubleToken(const char*& p, const char* end, double& out){
    p = skipSpaces(p, end);
    if(p >= end) return false;
    const char* start = p;
    p = skipToken(p, end);
    return parseDoubleToken(start, p, out);
}

inline bool parseBoundsLine(const LineView& line, double& lo, double& hi, double& tilt){
    const char* p = line.begin;
    if(!nextDoubleToken(p, line.end, lo)) return false;
    if(!nextDoubleToken(p, line.end, hi)) return false;
    if(!nextDoubleToken(p, line.end, tilt)) tilt = 0.0;
    return true;
}

inline void parseBoxFlags(const LineView& line, bool& pbcX, bool& pbcY, bool& pbcZ){
    pbcX = pbcY = pbcZ = true;
    std::string_view last[3] = {};
    int tokenCount = 0;
    const char* p = line.begin;
    while(p < line.end){
        p = skipSpaces(p, line.end);
        if(p >= line.end) break;
        const char* start = p;
        p = skipToken(p, line.end);
        std::string_view tok(start, static_cast<size_t>(p - start));
        if(tokenCount >= 3){
            last[0] = last[1];
            last[1] = last[2];
            last[2] = tok;
        }else{
            last[tokenCount] = tok;
        }
        ++tokenCount;
    }
    if(tokenCount >= 6){
        pbcX = (last[0] == "pp");
        pbcY = (last[1] == "pp");
        pbcZ = (last[2] == "pp");
    }
}

enum class ColumnKind : unsigned char{
    Ignore,
    Id,
    Type,
    PosX,
    PosY,
    PosZ,
    ImageX,
    ImageY,
    ImageZ,
    Extra
};

struct ExtraAtomColumn{
    std::string name;
    DataType dataType = DataType::Void;
};

struct AtomColumns{
    int idCol = -1;
    int typeCol = -1;
    int xCol = -1;
    int yCol = -1;
    int zCol = -1;
    int xsCol = -1;
    int ysCol = -1;
    int zsCol = -1;
    int ixCol = -1;
    int iyCol = -1;
    int izCol = -1;
    int posXCol = -1;
    int posYCol = -1;
    int posZCol = -1;
    bool scaled = false;
    bool hasImageFlags = false;
    bool valid = false;
    std::vector<std::string> columnNames;
    std::vector<ColumnKind> kinds;
    std::vector<int> extraIndices;
    std::vector<ExtraAtomColumn> extras;
};

inline DataType resolveExtraColumnType(std::string_view name){
    if(name == "correspondence"){
        return DataType::Int64;
    }
    if(name == "structure_type" || name == "cluster_id" || name.ends_with("_id")){
        return DataType::Int;
    }
    if(name.starts_with("orientation_")){
        return DataType::Double;
    }
    return DataType::Double;
}

inline AtomColumns parseAtomColumns(const LineView& line){
    AtomColumns cols;
    int tokenIndex = 0;
    int columnIndex = 0;
    const char* p = line.begin;
    cols.columnNames.clear();
    while(p < line.end){
        p = skipSpaces(p, line.end);
        if(p >= line.end) break;
        const char* start = p;
        p = skipToken(p, line.end);
        if(tokenIndex >= 2){
            std::string_view tok(start, static_cast<size_t>(p - start));
            if(tok == "id") cols.idCol = columnIndex;
            else if(tok == "type") cols.typeCol = columnIndex;
            else if(tok == "x") cols.xCol = columnIndex;
            else if(tok == "y") cols.yCol = columnIndex;
            else if(tok == "z") cols.zCol = columnIndex;
            else if(tok == "xs") cols.xsCol = columnIndex;
            else if(tok == "ys") cols.ysCol = columnIndex;
            else if(tok == "zs") cols.zsCol = columnIndex;
            else if(tok == "ix") cols.ixCol = columnIndex;
            else if(tok == "iy") cols.iyCol = columnIndex;
            else if(tok == "iz") cols.izCol = columnIndex;
            cols.columnNames.emplace_back(tok);
            ++columnIndex;
        }
        ++tokenIndex;
    }

    cols.scaled = (cols.xsCol >= 0 && cols.ysCol >= 0 && cols.zsCol >= 0);
    cols.posXCol = cols.scaled ? cols.xsCol : cols.xCol;
    cols.posYCol = cols.scaled ? cols.ysCol : cols.yCol;
    cols.posZCol = cols.scaled ? cols.zsCol : cols.zCol;
    cols.valid = (cols.posXCol >= 0 && cols.posYCol >= 0 && cols.posZCol >= 0);
    cols.hasImageFlags = (cols.ixCol >= 0 && cols.iyCol >= 0 && cols.izCol >= 0);
    cols.kinds.assign(static_cast<size_t>(columnIndex), ColumnKind::Ignore);
    cols.extraIndices.assign(static_cast<size_t>(columnIndex), -1);

    if(cols.idCol >= 0) cols.kinds[cols.idCol] = ColumnKind::Id;
    if(cols.typeCol >= 0) cols.kinds[cols.typeCol] = ColumnKind::Type;
    if(cols.posXCol >= 0) cols.kinds[cols.posXCol] = ColumnKind::PosX;
    if(cols.posYCol >= 0) cols.kinds[cols.posYCol] = ColumnKind::PosY;
    if(cols.posZCol >= 0) cols.kinds[cols.posZCol] = ColumnKind::PosZ;
    if(cols.hasImageFlags){
        cols.kinds[cols.ixCol] = ColumnKind::ImageX;
        cols.kinds[cols.iyCol] = ColumnKind::ImageY;
        cols.kinds[cols.izCol] = ColumnKind::ImageZ;
    }

    tokenIndex = 0;
    columnIndex = 0;
    p = line.begin;
    while(p < line.end){
        p = skipSpaces(p, line.end);
        if(p >= line.end) break;
        const char* start = p;
        p = skipToken(p, line.end);
        if(tokenIndex >= 2){
            const std::string_view tok(start, static_cast<size_t>(p - start));
            if(cols.kinds[columnIndex] == ColumnKind::Ignore){
                cols.kinds[columnIndex] = ColumnKind::Extra;
                cols.extraIndices[columnIndex] = static_cast<int>(cols.extras.size());
                cols.extras.push_back({std::string(tok), resolveExtraColumnType(tok)});
            }
            ++columnIndex;
        }
        ++tokenIndex;
    }

    return cols;
}

struct CellMatrix{
    double m00, m01, m02, m03;
    double m10, m11, m12, m13;
    double m20, m21, m22, m23;
};

struct DumpBoxBounds{
    double xloBound = 0.0;
    double xhiBound = 0.0;
    double yloBound = 0.0;
    double yhiBound = 0.0;
    double zloBound = 0.0;
    double zhiBound = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yz = 0.0;
    bool triclinic = false;
};

struct ExpandedExtraColumn{
    std::string_view name;
    const void* data = nullptr;
    DataType dataType = DataType::Void;
    std::size_t rowCount = 0;
    std::size_t stride = 0;
    std::size_t componentIndex = 0;
};

inline void initializeExtraAtomColumns(const AtomColumns& cols, LammpsParser::Frame& frame){
    frame.atomColumnOrder = cols.columnNames;
    frame.atomColumnsScaled = cols.scaled;
    frame.atomProperties.clear();
    if(cols.hasImageFlags){
        frame.imageX.assign(static_cast<std::size_t>(frame.natoms), 0);
        frame.imageY.assign(static_cast<std::size_t>(frame.natoms), 0);
        frame.imageZ.assign(static_cast<std::size_t>(frame.natoms), 0);
    }else{
        frame.imageX.clear();
        frame.imageY.clear();
        frame.imageZ.clear();
    }
    for(const auto& extra : cols.extras){
        auto& column = frame.atomProperties[extra.name];
        column.dataType = extra.dataType;

        switch(extra.dataType){
            case DataType::Int:
                column.ints.assign(static_cast<std::size_t>(frame.natoms), 0);
                break;
            case DataType::Int64:
                column.int64s.assign(static_cast<std::size_t>(frame.natoms), 0);
                break;
            case DataType::Double:
                column.doubles.assign(static_cast<std::size_t>(frame.natoms), 0.0);
                break;
            case DataType::Void:
                break;
        }
    }
}

inline bool storeExtraAtomValue(
    LammpsParser::Frame& frame,
    const AtomColumns& cols,
    int extraIndex,
    std::size_t atomIndex,
    const char* tokenStart,
    const char* tokenEnd
){
    if(extraIndex < 0 || extraIndex >= static_cast<int>(cols.extras.size())){
        return false;
    }

    const auto& extra = cols.extras[static_cast<std::size_t>(extraIndex)];
    auto it = frame.atomProperties.find(extra.name);
    if(it == frame.atomProperties.end()){
        return false;
    }

    auto& column = it->second;
    switch(extra.dataType){
        case DataType::Int: {
            int value = 0;
            if(!parseIntToken(tokenStart, tokenEnd, value)) return false;
            column.ints[atomIndex] = value;
            return true;
        }
        case DataType::Int64: {
            if(tokenStart >= tokenEnd) return false;
            bool negative = false;
            if(*tokenStart == '-'){
                negative = true;
                ++tokenStart;
            }
            std::int64_t value = 0;
            bool any = false;
            for(const char* p = tokenStart; p < tokenEnd; ++p){
                const char c = *p;
                if(c < '0' || c > '9') break;
                value = value * 10 + static_cast<std::int64_t>(c - '0');
                any = true;
            }
            if(!any) return false;
            column.int64s[atomIndex] = negative ? -value : value;
            return true;
        }
        case DataType::Double: {
            double value = 0.0;
            if(!parseDoubleToken(tokenStart, tokenEnd, value)) return false;
            column.doubles[atomIndex] = value;
            return true;
        }
        case DataType::Void:
            return false;
    }
    return false;
}

inline const char* advanceLines(const char* p, const char* end, size_t lines, bool& ok){
    size_t count = 0;
    while(count < lines && p < end){
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if(nl){
            p = nl + 1;
            ++count;
        }else{
            ++count;
            p = end;
            break;
        }
    }
    ok = (count == lines);
    return p;
}

inline size_t countNewlines(const char* p, const char* end){
    size_t count = 0;
    while(p < end){
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if(!nl) break;
        ++count;
        p = nl + 1;
    }
    return count;
}

inline int resolveThreads(){
    int threads = static_cast<int>(std::thread::hardware_concurrency());
    if(threads <= 0){
        threads = 1;
    }
    return threads;
}

inline std::size_t dataTypeSize(DataType dataType){
    switch(dataType){
        case DataType::Int:
            return sizeof(int);
        case DataType::Double:
            return sizeof(double);
        case DataType::Int64:
            return sizeof(std::int64_t);
        case DataType::Void:
            return 0;
    }
    return 0;
}

inline int resolveAtomId(const LammpsParser::Frame& frame, std::size_t index){
    return index < frame.ids.size()
        ? frame.ids[index]
        : static_cast<int>(index + 1);
}

inline int resolveAtomType(const LammpsParser::Frame& frame, std::size_t index){
    return index < frame.types.size()
        ? frame.types[index]
        : 1;
}

inline Point3 resolveAtomPosition(const LammpsParser::Frame& frame, std::size_t index){
    return index < frame.positions.size()
        ? frame.positions[index]
        : Point3(0.0, 0.0, 0.0);
}

inline DumpBoxBounds buildDumpBoxBounds(const SimulationCell& simulationCell){
    const auto& matrix = simulationCell.matrix();
    const auto& a = matrix.column(0);
    const auto& b = matrix.column(1);
    const auto& c = matrix.column(2);
    const auto& origin = matrix.column(3);

    const double xlo = origin.x();
    const double ylo = origin.y();
    const double zlo = origin.z();
    const double xhi = xlo + a.x();
    const double yhi = ylo + b.y();
    const double zhi = zlo + c.z();

    DumpBoxBounds bounds;
    bounds.xy = b.x();
    bounds.xz = c.x();
    bounds.yz = c.y();

    const double dxmin = std::min({0.0, bounds.xy, bounds.xz, bounds.xy + bounds.xz});
    const double dxmax = std::max({0.0, bounds.xy, bounds.xz, bounds.xy + bounds.xz});

    bounds.xloBound = xlo + dxmin;
    bounds.xhiBound = xhi + dxmax;
    bounds.yloBound = ylo + std::min(0.0, bounds.yz);
    bounds.yhiBound = yhi + std::max(0.0, bounds.yz);
    bounds.zloBound = zlo;
    bounds.zhiBound = zhi;
    bounds.triclinic =
        std::abs(bounds.xy) > EPSILON ||
        std::abs(bounds.xz) > EPSILON ||
        std::abs(bounds.yz) > EPSILON;

    return bounds;
}

inline bool expandExtraColumns(
    const std::vector<LammpsParser::ExtraColumn>& extraColumns,
    const std::vector<int>& propertyAtomIds,
    std::vector<ExpandedExtraColumn>& expandedColumns
){
    expandedColumns.clear();

    for(const auto& column : extraColumns){
        if(column.names.empty()){
            std::cerr << "Error: extra dump column has no names" << std::endl;
            return false;
        }
        if(column.data == nullptr){
            std::cerr << "Error: extra dump column has null data" << std::endl;
            return false;
        }
        if(column.dataType == DataType::Void){
            std::cerr << "Error: extra dump column has invalid data type" << std::endl;
            return false;
        }
        if(column.rowCount != propertyAtomIds.size()){
            std::cerr << "Error: extra dump column row count does not match atom-id mapping" << std::endl;
            return false;
        }
        if(column.componentCount == 0 || column.componentCount != column.names.size()){
            std::cerr << "Error: extra dump column component metadata is inconsistent" << std::endl;
            return false;
        }

        const std::size_t stride = column.stride != 0
            ? column.stride
            : column.componentCount * dataTypeSize(column.dataType);
        if(stride == 0){
            std::cerr << "Error: extra dump column stride resolved to zero" << std::endl;
            return false;
        }

        expandedColumns.reserve(expandedColumns.size() + column.names.size());
        for(std::size_t componentIndex = 0; componentIndex < column.names.size(); ++componentIndex){
            expandedColumns.push_back({
                column.names[componentIndex],
                column.data,
                column.dataType,
                column.rowCount,
                stride,
                componentIndex
            });
        }
    }

    return true;
}

inline void writeDefaultExtraValue(std::ostream& out, DataType dataType){
    switch(dataType){
        case DataType::Double:
            out << 0.0;
            break;
        case DataType::Int:
        case DataType::Int64:
        case DataType::Void:
            out << 0;
            break;
    }
}

inline void writeExtraValue(std::ostream& out, const ExpandedExtraColumn& column, std::size_t rowIndex){
    const auto* row = static_cast<const std::uint8_t*>(column.data) + rowIndex * column.stride;

    switch(column.dataType){
        case DataType::Int:
            out << reinterpret_cast<const int*>(row)[column.componentIndex];
            break;
        case DataType::Double:
            out << reinterpret_cast<const double*>(row)[column.componentIndex];
            break;
        case DataType::Int64:
            out << static_cast<std::uint64_t>(
                reinterpret_cast<const std::int64_t*>(row)[column.componentIndex]
            );
            break;
        case DataType::Void:
            out << 0;
            break;
    }
}

inline bool writeDump(
    const std::string& filename,
    const LammpsParser::Frame& frame,
    const std::vector<ExpandedExtraColumn>& extraColumns,
    const std::vector<int>* propertyRowsByAtomIndex,
    const std::vector<LammpsParser::ExtraHeader>& extraHeaders
){
    if(frame.natoms < 0){
        std::cerr << "Error: invalid atom count " << frame.natoms << std::endl;
        return false;
    }

    const auto atomCount = static_cast<std::size_t>(frame.natoms);
    if(propertyRowsByAtomIndex && propertyRowsByAtomIndex->size() != atomCount){
        std::cerr << "Error: atom-property lookup table size does not match frame atom count" << std::endl;
        return false;
    }

    std::ofstream out(filename, std::ios::binary);
    if(!out.is_open()){
        std::cerr << "Error: cannot open file " << filename << " for writing" << std::endl;
        return false;
    }

    std::vector<char> buffer(1 << 20);
    out.rdbuf()->pubsetbuf(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out << std::setprecision(std::numeric_limits<double>::max_digits10);

    const auto bounds = buildDumpBoxBounds(frame.simulationCell);
    const auto& pbc = frame.simulationCell.pbcFlags();

    out << "ITEM: TIMESTEP\n" << frame.timestep << '\n'
        << "ITEM: NUMBER OF ATOMS\n" << frame.natoms << '\n';

    for(const auto& header : extraHeaders){
        out << "ITEM: " << header.name << '\n'
            << header.value << '\n';
    }

    out << "ITEM: BOX BOUNDS";

    if(bounds.triclinic){
        out << " xy xz yz";
    }

    out << ' '
        << (pbc[0] ? "pp" : "ff") << ' '
        << (pbc[1] ? "pp" : "ff") << ' '
        << (pbc[2] ? "pp" : "ff") << '\n';

    if(bounds.triclinic){
        out << bounds.xloBound << ' ' << bounds.xhiBound << ' ' << bounds.xy << '\n'
            << bounds.yloBound << ' ' << bounds.yhiBound << ' ' << bounds.xz << '\n'
            << bounds.zloBound << ' ' << bounds.zhiBound << ' ' << bounds.yz << '\n';
    }else{
        out << bounds.xloBound << ' ' << bounds.xhiBound << '\n'
            << bounds.yloBound << ' ' << bounds.yhiBound << '\n'
            << bounds.zloBound << ' ' << bounds.zhiBound << '\n';
    }

    out << "ITEM: ATOMS id type x y z";
    for(const auto& column : extraColumns){
        out << ' ' << column.name;
    }
    out << '\n';

    for(std::size_t atomIndex = 0; atomIndex < atomCount; ++atomIndex){
        const int atomId = resolveAtomId(frame, atomIndex);
        const int atomType = resolveAtomType(frame, atomIndex);
        const Point3 position = resolveAtomPosition(frame, atomIndex);

        out << atomId << ' '
            << atomType << ' '
            << position.x() << ' '
            << position.y() << ' '
            << position.z();

        const int propertyRow = propertyRowsByAtomIndex
            ? (*propertyRowsByAtomIndex)[atomIndex]
            : -1;

        for(const auto& column : extraColumns){
            out << ' ';
            if(propertyRow < 0){
                writeDefaultExtraValue(out, column.dataType);
            }else{
                writeExtraValue(out, column, static_cast<std::size_t>(propertyRow));
            }
        }
        out << '\n';
    }

    return static_cast<bool>(out);
}

inline void mergeHeaders(
    const LammpsParser::Frame& frame,
    const std::vector<LammpsParser::ExtraHeader>& extraHeaders,
    std::vector<LammpsParser::ExtraHeader>& mergedHeaders
){
    std::unordered_map<std::string, std::string> values = frame.headerProperties;
    std::unordered_map<std::string, bool> seen;
    seen.reserve(values.size() + extraHeaders.size());

    for(const auto& header : extraHeaders){
        values[header.name] = header.value;
    }

    mergedHeaders.clear();
    mergedHeaders.reserve(values.size() + extraHeaders.size());
    for(const auto& name : frame.headerOrder){
        auto it = values.find(name);
        if(it == values.end()){
            continue;
        }
        mergedHeaders.push_back({name, it->second});
        seen[name] = true;
    }

    for(const auto& header : extraHeaders){
        if(seen.find(header.name) == seen.end()){
            mergedHeaders.push_back(header);
            seen[header.name] = true;
        }
    }
}

inline bool writeMergedDump(
    const std::string& filename,
    const LammpsParser::Frame& frame,
    const std::vector<ExpandedExtraColumn>& extraColumns,
    const std::vector<int>* propertyRowsByAtomIndex,
    const std::vector<LammpsParser::ExtraHeader>& extraHeaders,
    bool overwriteExistingColumns
){
    if(frame.natoms < 0){
        std::cerr << "Error: invalid atom count " << frame.natoms << std::endl;
        return false;
    }

    const auto atomCount = static_cast<std::size_t>(frame.natoms);
    if(propertyRowsByAtomIndex && propertyRowsByAtomIndex->size() != atomCount){
        std::cerr << "Error: atom-property lookup table size does not match frame atom count" << std::endl;
        return false;
    }

    std::ofstream out(filename, std::ios::binary);
    if(!out.is_open()){
        std::cerr << "Error: cannot open file " << filename << " for writing" << std::endl;
        return false;
    }

    std::vector<char> buffer(1 << 20);
    out.rdbuf()->pubsetbuf(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out << std::setprecision(std::numeric_limits<double>::max_digits10);

    const auto bounds = buildDumpBoxBounds(frame.simulationCell);
    const auto& pbc = frame.simulationCell.pbcFlags();

    out << "ITEM: TIMESTEP\n" << frame.timestep << '\n'
        << "ITEM: NUMBER OF ATOMS\n" << frame.natoms << '\n';

    std::vector<LammpsParser::ExtraHeader> mergedHeaders;
    mergeHeaders(frame, extraHeaders, mergedHeaders);
    for(const auto& header : mergedHeaders){
        out << "ITEM: " << header.name << '\n'
            << header.value << '\n';
    }

    out << "ITEM: BOX BOUNDS";

    if(bounds.triclinic){
        out << " xy xz yz";
    }

    out << ' '
        << (pbc[0] ? "pp" : "ff") << ' '
        << (pbc[1] ? "pp" : "ff") << ' '
        << (pbc[2] ? "pp" : "ff") << '\n';

    if(bounds.triclinic){
        out << bounds.xloBound << ' ' << bounds.xhiBound << ' ' << bounds.xy << '\n'
            << bounds.yloBound << ' ' << bounds.yhiBound << ' ' << bounds.xz << '\n'
            << bounds.zloBound << ' ' << bounds.zhiBound << ' ' << bounds.yz << '\n';
    }else{
        out << bounds.xloBound << ' ' << bounds.xhiBound << '\n'
            << bounds.yloBound << ' ' << bounds.yhiBound << '\n'
            << bounds.zloBound << ' ' << bounds.zhiBound << '\n';
    }

    std::unordered_map<std::string, const ExpandedExtraColumn*> extraByName;
    extraByName.reserve(extraColumns.size());
    for(const auto& column : extraColumns){
        extraByName[std::string(column.name)] = &column;
    }

    std::vector<std::string> columnOrder = frame.atomColumnOrder;
    if(columnOrder.empty()){
        columnOrder = { "id", "type", "x", "y", "z" };
    }

    std::unordered_map<std::string, std::size_t> columnIndex;
    columnIndex.reserve(columnOrder.size());
    for(std::size_t i = 0; i < columnOrder.size(); ++i){
        columnIndex[columnOrder[i]] = i;
    }

    for(const auto& column : extraColumns){
        const std::string columnName(column.name);
        if(columnIndex.find(columnName) == columnIndex.end()){
            columnIndex[columnName] = columnOrder.size();
            columnOrder.push_back(columnName);
        }
    }

    out << "ITEM: ATOMS";
    for(const auto& name : columnOrder){
        out << ' ' << name;
    }
    out << '\n';

    const auto& inverseCell = frame.simulationCell.inverseMatrix();

    for(std::size_t atomIndex = 0; atomIndex < atomCount; ++atomIndex){
        const int atomId = resolveAtomId(frame, atomIndex);
        const int atomType = resolveAtomType(frame, atomIndex);
        const Point3 position = resolveAtomPosition(frame, atomIndex);
        const Point3 reduced = inverseCell * position;

        const int propertyRow = propertyRowsByAtomIndex
            ? (*propertyRowsByAtomIndex)[atomIndex]
            : -1;

        for(std::size_t colIdx = 0; colIdx < columnOrder.size(); ++colIdx){
            if(colIdx > 0){
                out << ' ';
            }
            const std::string& name = columnOrder[colIdx];
            if(name == "id"){
                out << atomId;
                continue;
            }
            if(name == "type"){
                out << atomType;
                continue;
            }
            if(name == "x"){
                out << position.x();
                continue;
            }
            if(name == "y"){
                out << position.y();
                continue;
            }
            if(name == "z"){
                out << position.z();
                continue;
            }
            if(name == "xs"){
                out << reduced.x();
                continue;
            }
            if(name == "ys"){
                out << reduced.y();
                continue;
            }
            if(name == "zs"){
                out << reduced.z();
                continue;
            }

            auto extraIt = extraByName.find(name);
            if(extraIt != extraByName.end() && (overwriteExistingColumns ||
                frame.atomProperties.find(name) == frame.atomProperties.end())){
                const auto* column = extraIt->second;
                if(propertyRow < 0){
                    writeDefaultExtraValue(out, column->dataType);
                }else{
                    writeExtraValue(out, *column, static_cast<std::size_t>(propertyRow));
                }
                continue;
            }

            auto propIt = frame.atomProperties.find(name);
            if(propIt == frame.atomProperties.end()){
                out << 0;
                continue;
            }
            const auto& prop = propIt->second;
            if(atomIndex >= prop.size()){
                out << 0;
                continue;
            }
            switch(prop.dataType){
                case DataType::Int:
                    out << prop.ints[atomIndex];
                    break;
                case DataType::Int64:
                    out << static_cast<std::uint64_t>(prop.int64s[atomIndex]);
                    break;
                case DataType::Double:
                    out << prop.doubles[atomIndex];
                    break;
                case DataType::Void:
                    out << 0;
                    break;
            }
        }
        out << '\n';
    }

    return static_cast<bool>(out);
}

inline const char* parseAtomLine(
    const char* p,
    const char* end,
    const AtomColumns& cols,
    const CellMatrix& cell,
    LammpsParser::Frame& frame,
    size_t index,
    bool& ok
){
    int id = cols.idCol >= 0 ? 0 : static_cast<int>(index + 1);
    int type = cols.typeCol >= 0 ? 0 : 1;
    bool idSet = cols.idCol < 0;
    bool typeSet = cols.typeCol < 0;
    bool xSet = false;
    bool ySet = false;
    bool zSet = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int imageX = 0;
    int imageY = 0;
    int imageZ = 0;
    int col = 0;

    while(p < end){
        char c = *p;
        if(c == '\n'){
            ++p;
            break;
        }
        if(c == '\r'){
            ++p;
            continue;
        }
        if(c == ' ' || c == '\t'){
            ++p;
            continue;
        }

        const char* tokenStart = p;
        while(p < end){
            char cc = *p;
            if(cc == ' ' || cc == '\t' || cc == '\n' || cc == '\r') break;
            ++p;
        }
        const char* tokenEnd = p;

        if(col < static_cast<int>(cols.kinds.size())){
            switch(cols.kinds[col]){
                case ColumnKind::Id:
                    if(!parseIntToken(tokenStart, tokenEnd, id)) ok = false;
                    else idSet = true;
                    break;
                case ColumnKind::Type:
                    if(!parseIntToken(tokenStart, tokenEnd, type)) ok = false;
                    else typeSet = true;
                    break;
                case ColumnKind::PosX:
                    if(!parseDoubleToken(tokenStart, tokenEnd, x)) ok = false;
                    else xSet = true;
                    break;
                case ColumnKind::PosY:
                    if(!parseDoubleToken(tokenStart, tokenEnd, y)) ok = false;
                    else ySet = true;
                    break;
                case ColumnKind::PosZ:
                    if(!parseDoubleToken(tokenStart, tokenEnd, z)) ok = false;
                    else zSet = true;
                    break;
                case ColumnKind::ImageX:
                    parseIntToken(tokenStart, tokenEnd, imageX);
                    break;
                case ColumnKind::ImageY:
                    parseIntToken(tokenStart, tokenEnd, imageY);
                    break;
                case ColumnKind::ImageZ:
                    parseIntToken(tokenStart, tokenEnd, imageZ);
                    break;
                case ColumnKind::Extra:
                    if(!storeExtraAtomValue(frame, cols, cols.extraIndices[static_cast<std::size_t>(col)], index, tokenStart, tokenEnd)){
                        ok = false;
                    }
                    break;
                default:
                    break;
            }
        }
        ++col;
    }

    if(!idSet || !typeSet || !xSet || !ySet || !zSet){
        ok = false;
        return p;
    }

    if(cols.scaled){
        const double px = cell.m00 * x + cell.m01 * y + cell.m02 * z + cell.m03;
        const double py = cell.m10 * x + cell.m11 * y + cell.m12 * z + cell.m13;
        const double pz = cell.m20 * x + cell.m21 * y + cell.m22 * z + cell.m23;
        frame.positions[index] = Point3(px, py, pz);
    }else{
        frame.positions[index] = Point3(x, y, z);
    }

    if(cols.hasImageFlags){
        frame.imageX[index] = imageX;
        frame.imageY[index] = imageY;
        frame.imageZ[index] = imageZ;
    }

    frame.ids[index] = id;
    frame.types[index] = type;
    return p;
}

inline bool parseHeaderIntValue(const std::string& line, std::string_view suffix, int& value){
    const std::string clean = stripCommentCopy(line);
    if(clean.empty()) return false;

    std::istringstream ss(clean);
    int parsed = 0;
    std::string word;
    if(!(ss >> parsed >> word)){
        return false;
    }

    if(toLowerCopy(word) != suffix){
        return false;
    }

    value = parsed;
    return true;
}

inline bool parseDataBoundsLine(
    const std::string& line,
    const char* lowName,
    const char* highName,
    double& lo,
    double& hi
){
    const std::string clean = stripCommentCopy(line);
    if(clean.empty()) return false;

    std::istringstream ss(clean);
    std::string lowToken;
    std::string highToken;
    double parsedLo = 0.0;
    double parsedHi = 0.0;
    if(!(ss >> parsedLo >> parsedHi >> lowToken >> highToken)){
        return false;
    }

    if(toLowerCopy(lowToken) != lowName || toLowerCopy(highToken) != highName){
        return false;
    }

    lo = parsedLo;
    hi = parsedHi;
    return true;
}

inline bool parseDataTiltLine(const std::string& line, double& xy, double& xz, double& yz){
    const std::string clean = stripCommentCopy(line);
    if(clean.empty()) return false;

    std::istringstream ss(clean);
    std::string xyToken;
    std::string xzToken;
    std::string yzToken;
    double parsedXy = 0.0;
    double parsedXz = 0.0;
    double parsedYz = 0.0;
    if(!(ss >> parsedXy >> parsedXz >> parsedYz >> xyToken >> xzToken >> yzToken)){
        return false;
    }

    if(toLowerCopy(xyToken) != "xy" || toLowerCopy(xzToken) != "xz" || toLowerCopy(yzToken) != "yz"){
        return false;
    }

    xy = parsedXy;
    xz = parsedXz;
    yz = parsedYz;
    return true;
}

inline std::string parseDataSectionName(const std::string& line){
    const std::string clean = stripCommentCopy(line);
    if(clean.empty()) return {};

    std::istringstream ss(clean);
    std::string section;
    ss >> section;
    return toLowerCopy(section);
}

inline std::string parseAtomsStyle(const std::string& line){
    const std::size_t comment = line.find('#');
    if(comment == std::string::npos){
        return {};
    }
    return toLowerCopy(trimCopy(std::string_view(line).substr(comment + 1)));
}

inline std::string dataAtomsHeaderForStyle(const std::string& style){
    const std::string normalized = toLowerCopy(style);
    if(normalized == "charge"){
        return "ITEM: ATOMS id type q x y z";
    }
    if(normalized == "molecular" || normalized == "bond" || normalized == "angle"){
        return "ITEM: ATOMS id molecule_id type x y z";
    }
    if(normalized == "full"){
        return "ITEM: ATOMS id molecule_id type q x y z";
    }
    return "ITEM: ATOMS id type x y z";
}


/**
 * Converts a frame from the shared reader into the Frame this toolkit works with.
 *
 * The two models line up closely; the differences worth knowing:
 *  - positions arrive as a flat xyz buffer and become Point3, one copy, no conversion
 *  - types and ids are int here and unsigned there, which is fine for LAMMPS ranges
 *  - the reader hands back Cartesian positions even when the file held fractional ones,
 *    so atomColumnsScaled records the *source* for a caller re-emitting the file
 *  - the reader has no concept of an "extra header", so unrecognized ITEM sections travel
 *    as ordered name/value pairs and are unpacked into headerOrder/headerProperties
 */
inline bool toFrame(const lammpsio::ParsedFrame& parsed,
                   const lammpsio::VectorFrameAllocator& allocator,
                   LammpsParser::Frame& frame){
    const int natoms = parsed.header.atomCount;
    const std::vector<double>& coordinates = allocator.positions();

    if((int)allocator.types().size() != natoms ||
       (int)coordinates.size() != natoms * 3){
        std::cerr << "Error: reader returned " << allocator.types().size()
                  << " atoms for a frame declaring " << natoms << std::endl;
        return false;
    }

    frame.timestep = parsed.header.timestep;
    frame.natoms = natoms;

    frame.positions.resize((std::size_t)natoms);
    frame.types.resize((std::size_t)natoms);
    for(int i = 0; i < natoms; ++i){
        frame.positions[(std::size_t)i] = Point3(
            coordinates[(std::size_t)i * 3],
            coordinates[(std::size_t)i * 3 + 1],
            coordinates[(std::size_t)i * 3 + 2]
        );
        frame.types[(std::size_t)i] = (int)allocator.types()[(std::size_t)i];
    }

    frame.ids.clear();
    if(allocator.ids().size() == (std::size_t)natoms){
        frame.ids.reserve((std::size_t)natoms);
        for(std::uint32_t id : allocator.ids()) frame.ids.push_back((int)id);
    }

    const Vector3 a(parsed.header.cell[0][0], parsed.header.cell[0][1], parsed.header.cell[0][2]);
    const Vector3 b(parsed.header.cell[1][0], parsed.header.cell[1][1], parsed.header.cell[1][2]);
    const Vector3 c(parsed.header.cell[2][0], parsed.header.cell[2][1], parsed.header.cell[2][2]);
    const Point3 origin(parsed.header.origin[0], parsed.header.origin[1], parsed.header.origin[2]);
    frame.simulationCell.setMatrix(AffineTransformation(a, b, c, origin - Point3::Origin()));
    frame.simulationCell.setPbcFlags(parsed.header.periodic[0],
                                     parsed.header.periodic[1],
                                     parsed.header.periodic[2]);

    frame.headerOrder.clear();
    frame.headerProperties.clear();
    for(const auto& section : parsed.header.extraSections){
        frame.headerOrder.push_back(section.first);
        frame.headerProperties[section.first] = section.second;
    }

    frame.atomColumnOrder = parsed.header.headers;
    frame.atomColumnsScaled = parsed.header.positionsWereScaled;

    frame.atomProperties.clear();
    frame.imageX.clear();
    frame.imageY.clear();
    frame.imageZ.clear();

    for(const auto& column : parsed.extras){
        LammpsParser::AtomColumn converted;
        if(column.dtype == lammpsio::ColumnDtype::Int32){
            converted.dataType = DataType::Int;
            converted.ints.reserve(column.values.size());
            for(double value : column.values) converted.ints.push_back((int)value);
        }else{
            converted.dataType = DataType::Double;
            converted.doubles = column.values;
        }
        frame.atomProperties[column.name] = std::move(converted);

        // Periodic image flags get their own fields: the multi-frame transforms reach for
        // them directly rather than through the column map.
        std::vector<int>* image = nullptr;
        if(column.name == "ix") image = &frame.imageX;
        else if(column.name == "iy") image = &frame.imageY;
        else if(column.name == "iz") image = &frame.imageZ;

        if(image){
            image->reserve(column.values.size());
            for(double value : column.values) image->push_back((int)value);
        }
    }

    return true;
}

} // namespace LammpsParserDetail

using namespace LammpsParserDetail;

// Reads any trajectory format the shared reader supports into a Frame.
//
// The parsing itself lives in lammpsio, the same code the daemon's Node addon uses, so a
// format fix or a new format lands once for both. What stays here is the conversion into
// this Frame — the shape ten plugins are written against — and the writers, which the
// reader has no counterpart for.
bool LammpsParser::parseFile(const std::string &filename, Frame &frame){
    lammpsio::ReadOptions options;
    options.includeIds = true;
    // Every per-atom column the file carries: a plugin asks for its own by name later, and
    // the round-trip writer needs all of them.
    options.properties = { "*" };
    // One thread: callers are already inside a parallel region more often than not, and a
    // nested pool would fight the one that scheduled them.
    options.maxThreads = 1;

    // float64 positions. The geometry in this toolkit is double throughout, and strain and
    // dislocation analysis are sensitive to the difference.
    lammpsio::VectorFrameAllocator allocator(lammpsio::PositionPrecision::Float64);
    lammpsio::ParsedFrame parsed;
    std::string error;

    if(!lammpsio::readFrame(filename.c_str(), options, allocator, parsed, error)){
        std::cerr << "Error: " << error << std::endl;
        return false;
    }

    return LammpsParserDetail::toFrame(parsed, allocator, frame);
}

bool LammpsParser::writeFile(const std::string& filename, const Frame& frame){
    return writeDump(filename, frame, {}, nullptr, {});
}

bool LammpsParser::writeFileWithExtraColumns(
    const std::string& filename,
    const Frame& frame,
    const std::vector<int>& propertyAtomIds,
    const std::vector<ExtraColumn>& extraColumns,
    const std::vector<ExtraHeader>& extraHeaders
){
    if(frame.natoms < 0){
        std::cerr << "Error: invalid atom count " << frame.natoms << std::endl;
        return false;
    }

    std::vector<int> resolvedPropertyAtomIds;
    const std::vector<int>* atomIdsForMapping = &propertyAtomIds;
    if(!extraColumns.empty() && propertyAtomIds.empty()){
        resolvedPropertyAtomIds.resize(static_cast<std::size_t>(frame.natoms));
        for(std::size_t atomIndex = 0; atomIndex < resolvedPropertyAtomIds.size(); ++atomIndex){
            resolvedPropertyAtomIds[atomIndex] = resolveAtomId(frame, atomIndex);
        }
        atomIdsForMapping = &resolvedPropertyAtomIds;
    }

    std::vector<ExpandedExtraColumn> expandedColumns;
    if(!expandExtraColumns(extraColumns, *atomIdsForMapping, expandedColumns)){
        return false;
    }

    std::unordered_map<int, int> propertyRowByAtomId;
    propertyRowByAtomId.reserve(atomIdsForMapping->size());

    for(std::size_t rowIndex = 0; rowIndex < atomIdsForMapping->size(); ++rowIndex){
        const int atomId = (*atomIdsForMapping)[rowIndex];
        auto [it, inserted] = propertyRowByAtomId.emplace(atomId, static_cast<int>(rowIndex));
        if(!inserted){
            std::cerr << "Error: duplicate atom id " << atomId
                      << " in extra dump property mapping" << std::endl;
            return false;
        }
    }

    std::vector<int> propertyRowsByAtomIndex(static_cast<std::size_t>(frame.natoms), -1);
    for(std::size_t atomIndex = 0; atomIndex < propertyRowsByAtomIndex.size(); ++atomIndex){
        const int atomId = resolveAtomId(frame, atomIndex);
        auto it = propertyRowByAtomId.find(atomId);
        if(it != propertyRowByAtomId.end()){
            propertyRowsByAtomIndex[atomIndex] = it->second;
        }
    }

    return writeDump(filename, frame, expandedColumns, &propertyRowsByAtomIndex, extraHeaders);
}

bool LammpsParser::writeFileMergedWithExtraColumns(
    const std::string& filename,
    const Frame& frame,
    const std::vector<int>& propertyAtomIds,
    const std::vector<ExtraColumn>& extraColumns,
    const std::vector<ExtraHeader>& extraHeaders,
    bool overwriteExistingColumns
){
    if(frame.natoms < 0){
        std::cerr << "Error: invalid atom count " << frame.natoms << std::endl;
        return false;
    }

    std::vector<int> resolvedPropertyAtomIds;
    const std::vector<int>* atomIdsForMapping = &propertyAtomIds;
    if(!extraColumns.empty() && propertyAtomIds.empty()){
        resolvedPropertyAtomIds.resize(static_cast<std::size_t>(frame.natoms));
        for(std::size_t atomIndex = 0; atomIndex < resolvedPropertyAtomIds.size(); ++atomIndex){
            resolvedPropertyAtomIds[atomIndex] = resolveAtomId(frame, atomIndex);
        }
        atomIdsForMapping = &resolvedPropertyAtomIds;
    }

    std::vector<ExpandedExtraColumn> expandedColumns;
    if(!expandExtraColumns(extraColumns, *atomIdsForMapping, expandedColumns)){
        return false;
    }

    std::unordered_map<int, int> propertyRowByAtomId;
    propertyRowByAtomId.reserve(atomIdsForMapping->size());

    for(std::size_t rowIndex = 0; rowIndex < atomIdsForMapping->size(); ++rowIndex){
        const int atomId = (*atomIdsForMapping)[rowIndex];
        auto [it, inserted] = propertyRowByAtomId.emplace(atomId, static_cast<int>(rowIndex));
        if(!inserted){
            std::cerr << "Error: duplicate atom id " << atomId
                      << " in extra dump property mapping" << std::endl;
            return false;
        }
    }

    std::vector<int> propertyRowsByAtomIndex(static_cast<std::size_t>(frame.natoms), -1);
    for(std::size_t atomIndex = 0; atomIndex < propertyRowsByAtomIndex.size(); ++atomIndex){
        const int atomId = resolveAtomId(frame, atomIndex);
        auto it = propertyRowByAtomId.find(atomId);
        if(it != propertyRowByAtomId.end()){
            propertyRowsByAtomIndex[atomIndex] = it->second;
        }
    }

    return writeMergedDump(
        filename,
        frame,
        expandedColumns,
        &propertyRowsByAtomIndex,
        extraHeaders,
        overwriteExistingColumns
    );
}

// Parse a LAMMPS dump from any input stream.
// Return header lines, box bounds, and atom data in sequence.
// If any stage fails, the function aborts and returns false.

}
