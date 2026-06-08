#pragma once

#include <volt/utilities/json_utils.h>
#include <volt/core/lammps_parser.h>
#include <volt/math/point3.h>

#include <functional>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace Volt {

// Assigns a bucket name to each atom (e.g. "All", "VALID", "Coordination_6").
using BucketResolver = std::function<std::string(std::size_t atomIndex)>;

// Writes extra per-atom msgpack key/value pairs directly to the stream.
// Must set extraCount to the exact number of key/value pairs written.
using AtomExtraFieldWriter = std::function<void(MsgpackWriter& w, std::size_t atomIndex, int& extraCount)>;

// Writes the per-atom-properties record fields for an atom (the scalar/vector
// coloring set surfaced in the canvas). When supplied to streamAtomsToFile it
// REPLACES the default structure_id/structure_name/cluster_id fields; only the
// leading atom "id" key is kept. Must set count to the exact number of
// key/value pairs written.
using PerAtomPropertyWriter = std::function<void(MsgpackWriter& w, std::size_t atomIndex, int& count)>;

// Streams an _atoms.msgpack file directly without building a JSON DOM.
// Produces the canonical AtomisticExporter format consumed by the Volt viewer:
//   { main_listing, sub_listings, export: { AtomisticExporter: { bucket: [...] } } }
//
// base fields per atom: id, pos, structure_id, structure_name, cluster_id (5)
// extra fields: emitted by writeExtraFields callback
inline void streamAtomsToFile(
    const std::string& filePath,
    const LammpsParser::Frame& frame,
    BucketResolver resolveBucket,
    AtomExtraFieldWriter writeExtraFields = {},
    PerAtomPropertyWriter writePerAtomProperties = {}
){
    const std::size_t natoms = static_cast<std::size_t>(frame.natoms);

    // Pass 1: group atom indices by bucket (single scan)
    std::map<std::string, std::vector<std::size_t>> bucketAtoms;
    for(std::size_t i = 0; i < natoms; ++i){
        bucketAtoms[resolveBucket(i)].push_back(i);
    }

    // Probe extra field count using a null sink on atom 0
    int extraFields = 0;
    if(writeExtraFields && natoms > 0){
        struct NullBuf : std::streambuf{
            int overflow(int c) override{ return c; }
        } nullBuf;
        std::ostream nullStream(&nullBuf);
        MsgpackWriter probe(nullStream);
        writeExtraFields(probe, 0, extraFields);
    }
    const int totalFieldsPerAtom = 5 + extraFields;
    const uint32_t numBuckets = static_cast<uint32_t>(bucketAtoms.size());

    // Pass 2: stream to file
    std::ofstream of(filePath, std::ios::binary);
    MsgpackWriter w(of);

    w.write_map_header(4);

    w.write_key("main_listing");
    w.write_map_header(2);
    w.write_key("total_atoms"); w.write_int(static_cast<int64_t>(natoms));
    w.write_key("structure_count"); w.write_int(numBuckets);

    w.write_key("sub_listings");
    w.write_map_header(1);
    w.write_key("structures");
    w.write_array_header(numBuckets);
    int bucketId = 0;
    for(const auto& [name, indices] : bucketAtoms){
        w.write_map_header(3);
        w.write_key("structure_id"); w.write_int(bucketId++);
        w.write_key("structure_name"); w.write_str(name);
        w.write_key("atom_count"); w.write_int(static_cast<int64_t>(indices.size()));
    }

    w.write_key("export");
    w.write_map_header(1);
    w.write_key("AtomisticExporter");
    w.write_map_header(numBuckets);

    bucketId = 0;
    for(const auto& [name, indices] : bucketAtoms){
        w.write_key(name);
        w.write_array_header(static_cast<uint32_t>(indices.size()));
        for(std::size_t i : indices){
            w.write_map_header(static_cast<uint32_t>(totalFieldsPerAtom));
            w.write_key("id");
            w.write_int(i < frame.ids.size() ? frame.ids[i] : static_cast<int>(i));
            w.write_key("pos");
            w.write_array_header(3);
            const auto& pos = i < frame.positions.size() ? frame.positions[i] : Point3::Origin();
            w.write_double(pos.x()); w.write_double(pos.y()); w.write_double(pos.z());
            w.write_key("structure_id"); w.write_int(bucketId);
            w.write_key("structure_name"); w.write_str(name);
            w.write_key("cluster_id"); w.write_int(0);
            if(writeExtraFields){
                int dummy = 0;
                writeExtraFields(w, i, dummy);
            }
        }
        ++bucketId;
    }

    // per-atom-properties: flat array indexed by atom, carrying scalar fields for /canvas coloring
    std::vector<int> atomBucketId(natoms, 0);
    std::vector<const std::string*> bucketNameById;
    bucketNameById.reserve(numBuckets);
    {
        int bid = 0;
        for(const auto& [name, indices] : bucketAtoms){
            bucketNameById.push_back(&name);
            for(std::size_t i : indices) atomBucketId[i] = bid;
            ++bid;
        }
    }

    w.write_key("per-atom-properties");
    w.write_array_header(static_cast<uint32_t>(natoms));
    if(writePerAtomProperties){
        // Plugin-defined per-atom-properties: keep only the leading atom id and
        // let the plugin emit the meaningful fields (e.g. csp / coordination /
        // cluster_id) without the generic structure_id/structure_name base.
        int perAtomFieldCount = 0;
        if(natoms > 0){
            struct NullBufPA : std::streambuf{
                int overflow(int c) override{ return c; }
            } nullBufPA;
            std::ostream nullStreamPA(&nullBufPA);
            MsgpackWriter probePA(nullStreamPA);
            writePerAtomProperties(probePA, 0, perAtomFieldCount);
        }
        for(std::size_t i = 0; i < natoms; ++i){
            w.write_map_header(static_cast<uint32_t>(1 + perAtomFieldCount));
            w.write_key("id");
            w.write_int(i < frame.ids.size() ? frame.ids[i] : static_cast<int>(i));
            int dummy = 0;
            writePerAtomProperties(w, i, dummy);
        }
    }else{
        for(std::size_t i = 0; i < natoms; ++i){
            w.write_map_header(static_cast<uint32_t>(4 + extraFields));
            w.write_key("id");
            w.write_int(i < frame.ids.size() ? frame.ids[i] : static_cast<int>(i));
            w.write_key("structure_id"); w.write_int(atomBucketId[i]);
            w.write_key("structure_name"); w.write_str(*bucketNameById[atomBucketId[i]]);
            w.write_key("cluster_id"); w.write_int(0);
            if(writeExtraFields){
                int dummy = 0;
                writeExtraFields(w, i, dummy);
            }
        }
    }

    of.flush();
}

}
