#pragma once

#include <volt/core/run_log.h>

#include <cstddef>
#include <string>

namespace duckdb{
class Appender;
}

namespace Volt{

class ColumnarRowWriter;

// What a Parquet writer wrote, for its indented line in the execution log.
struct ParquetAtomRows{
    std::size_t rows;
    std::size_t columns;
    std::string path;
};

struct ParquetBondRows{
    std::size_t rows;
    std::size_t columns;
    std::string path;
};

struct ParquetLineRows{
    std::size_t rows;
    std::size_t columns;
    std::size_t points;
    std::string path;
};

struct ParquetMeshTables{
    std::size_t vertices;
    std::string verticesPath;
    std::size_t facets;
    std::string facetsPath;
};

struct ParquetJsonPayload{
    std::size_t bytes;
    std::string path;
};

// The log of one Parquet write: a writer makes one when it starts and hands it what it wrote once the file is done.
class ParquetWriterLog : public RunLog{
public:
    using RunLog::dump;

    // The atom rows, collected and then loaded into DuckDB: the atoms table's line times both phases.
    void dump(const ColumnarRowWriter& collectedRows);
    void dump(const duckdb::Appender& loadedRows);
    void dump(const ParquetAtomRows& table);
    void dump(const ParquetBondRows& table);
    void dump(const ParquetLineRows& table);
    void dump(const ParquetMeshTables& tables);
    // Only a payload large enough to take a while gets a line.
    void dump(const ParquetJsonPayload& payload);

private:
    // Summaries are a few KiB and write in milliseconds; only per-atom or per-facet payloads take long enough to be
    // worth a line in the execution log.
    static constexpr std::size_t kReportedPayloadBytes = std::size_t{16} << 20;

    std::string _collected;
    std::string _loaded;
};

}
