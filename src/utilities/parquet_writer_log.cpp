#include <volt/utilities/parquet_writer_log.h>
#include <volt/utilities/byte_size.h>

#include <spdlog/spdlog.h>

namespace Volt{

void ParquetWriterLog::dump(const ColumnarRowWriter&){
    _collected = phaseElapsed();
}

void ParquetWriterLog::dump(const duckdb::Appender&){
    _loaded = phaseElapsed();
}

void ParquetWriterLog::dump(const ParquetAtomRows& table){
    spdlog::info("  Wrote {} atom rows x {} columns ({}) in {}: collect {}, DuckDB load {}, Parquet {}",
        table.rows, table.columns, Detail::writtenFileSize(table.path), runElapsed(),
        _collected, _loaded, phaseElapsed());
}

void ParquetWriterLog::dump(const ParquetBondRows& table){
    spdlog::info("  Wrote {} bond rows x {} columns ({}) in {}",
        table.rows, table.columns, Detail::writtenFileSize(table.path), runElapsed());
}

void ParquetWriterLog::dump(const ParquetLineRows& table){
    spdlog::info("  Wrote {} line rows x {} columns, {} points ({}) in {}",
        table.rows, table.columns, table.points, Detail::writtenFileSize(table.path), runElapsed());
}

void ParquetWriterLog::dump(const ParquetMeshTables& tables){
    spdlog::info("  Wrote mesh tables: {} vertices ({}), {} facets ({}) in {}",
        tables.vertices, Detail::writtenFileSize(tables.verticesPath),
        tables.facets, Detail::writtenFileSize(tables.facetsPath), runElapsed());
}

void ParquetWriterLog::dump(const ParquetJsonPayload& payload){
    if(payload.bytes >= kReportedPayloadBytes){
        spdlog::info("  Wrote {} JSON payload as parquet ({}) in {}",
            Detail::formatByteSize(payload.bytes), Detail::writtenFileSize(payload.path), runElapsed());
    }
}

}
