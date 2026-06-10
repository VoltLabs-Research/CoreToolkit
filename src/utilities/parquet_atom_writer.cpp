#include <volt/utilities/parquet_atom_writer.h>
#include <volt/math/point3.h>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

#include <map>
#include <memory>
#include <unordered_map>

namespace Volt {

namespace {

enum class ColType { Double, Int64, String, ListDouble };

struct DynColumn {
    std::string name;
    ColType type;
    std::shared_ptr<arrow::ArrayBuilder> builder;
    arrow::DoubleBuilder* listValue = nullptr; // for ListDouble
    bool touchedThisRow = false;
};

} // namespace

struct ColumnarAtomWriter::Impl {
    arrow::MemoryPool* pool = arrow::default_memory_pool();
    std::vector<DynColumn> columns;
    std::unordered_map<std::string, std::size_t> index;
    std::int64_t rowsCompleted = 0;

    // Fixed columns owned by streamAtomsToParquet; a plugin must not shadow them.
    static bool isReserved(const std::string& name){
        return name == "atom_index" || name == "id" || name == "x" || name == "y"
            || name == "z" || name == "bucket" || name == "structure_id"
            || name == "structure_name";
    }

    DynColumn& ensure(const std::string& name, ColType type){
        auto it = index.find(name);
        if(it != index.end()) return columns[it->second];

        DynColumn col;
        col.name = name;
        col.type = type;
        switch(type){
            case ColType::Double: col.builder = std::make_shared<arrow::DoubleBuilder>(pool); break;
            case ColType::Int64:  col.builder = std::make_shared<arrow::Int64Builder>(pool); break;
            case ColType::String: col.builder = std::make_shared<arrow::StringBuilder>(pool); break;
            case ColType::ListDouble: {
                auto value = std::make_shared<arrow::DoubleBuilder>(pool);
                col.listValue = value.get();
                col.builder = std::make_shared<arrow::ListBuilder>(pool, value);
                break;
            }
        }
        // Back-fill nulls for rows that completed before this column appeared.
        for(std::int64_t r = 0; r < rowsCompleted; ++r) (void)col.builder->AppendNull();

        index.emplace(name, columns.size());
        columns.push_back(std::move(col));
        return columns.back();
    }

    void appendDouble(const std::string& name, double v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Double);
        (void)static_cast<arrow::DoubleBuilder*>(c.builder.get())->Append(v);
        c.touchedThisRow = true;
    }
    void appendInt64(const std::string& name, std::int64_t v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::Int64);
        if(c.type == ColType::Double){
            (void)static_cast<arrow::DoubleBuilder*>(c.builder.get())->Append(static_cast<double>(v));
        } else {
            (void)static_cast<arrow::Int64Builder*>(c.builder.get())->Append(v);
        }
        c.touchedThisRow = true;
    }
    void appendString(const std::string& name, const std::string& v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::String);
        (void)static_cast<arrow::StringBuilder*>(c.builder.get())->Append(v);
        c.touchedThisRow = true;
    }
    void appendList(const std::string& name, const std::vector<double>& v){
        if(isReserved(name)) return;
        auto& c = ensure(name, ColType::ListDouble);
        auto* lb = static_cast<arrow::ListBuilder*>(c.builder.get());
        (void)lb->Append();
        (void)c.listValue->AppendValues(v.data(), static_cast<std::int64_t>(v.size()));
        c.touchedThisRow = true;
    }

    void finishRow(){
        for(auto& c : columns){
            if(!c.touchedThisRow) (void)c.builder->AppendNull();
            c.touchedThisRow = false;
        }
        ++rowsCompleted;
    }
};

void ColumnarAtomWriter::field(const std::string& name, double value){ _impl.appendDouble(name, value); }
void ColumnarAtomWriter::field(const std::string& name, std::int64_t value){ _impl.appendInt64(name, value); }
void ColumnarAtomWriter::field(const std::string& name, const std::string& value){ _impl.appendString(name, value); }
void ColumnarAtomWriter::field(const std::string& name, const std::vector<double>& values){ _impl.appendList(name, values); }

void streamAtomsToParquet(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    const BucketResolver& resolveBucket,
    const PerAtomColumnWriter& writePerAtomColumns,
    const StructureIdResolver& resolveStructureId
){
    const std::size_t natoms = static_cast<std::size_t>(frame.natoms);
    arrow::MemoryPool* pool = arrow::default_memory_pool();

    // Stable structure_id per bucket, assigned in first-seen order.
    std::map<std::string, std::int32_t> bucketId;
    auto idForBucket = [&](const std::string& name) -> std::int32_t {
        auto it = bucketId.find(name);
        if(it != bucketId.end()) return it->second;
        const std::int32_t id = static_cast<std::int32_t>(bucketId.size());
        bucketId.emplace(name, id);
        return id;
    };

    arrow::UInt32Builder atomIndexB(pool);
    arrow::UInt64Builder idB(pool);
    arrow::DoubleBuilder xB(pool), yB(pool), zB(pool);
    arrow::StringBuilder bucketB(pool);
    arrow::Int32Builder structureIdB(pool);
    arrow::StringBuilder structureNameB(pool);

    ColumnarAtomWriter::Impl dyn;
    ColumnarAtomWriter writer(dyn);

    for(std::size_t i = 0; i < natoms; ++i){
        const std::string bucket = resolveBucket(i);
        const std::int32_t sid = resolveStructureId
            ? static_cast<std::int32_t>(resolveStructureId(i))
            : idForBucket(bucket);

        (void)atomIndexB.Append(static_cast<std::uint32_t>(i));
        (void)idB.Append(i < frame.ids.size()
            ? static_cast<std::uint64_t>(frame.ids[i])
            : static_cast<std::uint64_t>(i));
        const auto& pos = i < frame.positions.size() ? frame.positions[i] : Point3::Origin();
        (void)xB.Append(pos.x());
        (void)yB.Append(pos.y());
        (void)zB.Append(pos.z());
        (void)bucketB.Append(bucket);
        (void)structureIdB.Append(sid);
        (void)structureNameB.Append(bucket);

        if(writePerAtomColumns) writePerAtomColumns(writer, i);
        dyn.finishRow();
    }

    std::vector<std::shared_ptr<arrow::Field>> fields = {
        arrow::field("atom_index", arrow::uint32()),
        arrow::field("id", arrow::uint64()),
        arrow::field("x", arrow::float64()),
        arrow::field("y", arrow::float64()),
        arrow::field("z", arrow::float64()),
        arrow::field("bucket", arrow::utf8()),
        arrow::field("structure_id", arrow::int32()),
        arrow::field("structure_name", arrow::utf8())
    };
    std::vector<std::shared_ptr<arrow::Array>> arrays(8);
    if(!atomIndexB.Finish(&arrays[0]).ok()) return;
    if(!idB.Finish(&arrays[1]).ok()) return;
    if(!xB.Finish(&arrays[2]).ok()) return;
    if(!yB.Finish(&arrays[3]).ok()) return;
    if(!zB.Finish(&arrays[4]).ok()) return;
    if(!bucketB.Finish(&arrays[5]).ok()) return;
    if(!structureIdB.Finish(&arrays[6]).ok()) return;
    if(!structureNameB.Finish(&arrays[7]).ok()) return;

    for(auto& col : dyn.columns){
        std::shared_ptr<arrow::Array> arr;
        if(!col.builder->Finish(&arr).ok()) return;
        std::shared_ptr<arrow::DataType> dt;
        switch(col.type){
            case ColType::Double: dt = arrow::float64(); break;
            case ColType::Int64:  dt = arrow::int64(); break;
            case ColType::String: dt = arrow::utf8(); break;
            case ColType::ListDouble: dt = arrow::list(arrow::float64()); break;
        }
        fields.push_back(arrow::field(col.name, dt));
        arrays.push_back(arr);
    }

    auto schema = arrow::schema(fields);
    auto table = arrow::Table::Make(schema, arrays);

    auto outResult = arrow::io::FileOutputStream::Open(filePath);
    if(!outResult.ok()) return;
    std::shared_ptr<arrow::io::FileOutputStream> out = *outResult;

    parquet::WriterProperties::Builder props;
    props.compression(parquet::Compression::ZSTD);
    (void)parquet::arrow::WriteTable(*table, pool, out,
        natoms > 0 ? static_cast<std::int64_t>(natoms) : 1, props.build());
}

}
