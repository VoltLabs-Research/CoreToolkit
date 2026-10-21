// The DuckDB a wasm build queries (compat/wasm/duckdb.hpp sends its SQL here): duckdb-wasm's
// synchronous Node build, which the program requires as Node does, from its node_modules or the
// machine's global packages. It opens before main(), asynchronously, which is how it instantiates;
// queries then run synchronously. Each duckdb::DuckDB is an in-memory database of its own, attached
// to the engine. Results come back as JSON: {"columns":[{"name","type"}],"rows":[[...]]}, or
// {"error": "..."}.
addToLibrary({
    $voltDuckDB: {
        // A plain object: the library is written out as source, and a Map would not survive it.
        connections: {},
        nextDatabase: 1,
        nextConnection: 1,
        nextFile: 1,

        bindings: null,

        start() {
            addRunDependency('volt-duckdb');
            const entry = '@duckdb/duckdb-wasm/dist/duckdb-node-blocking.cjs';
            const duckdb = require(entry);
            const dist = require('path').dirname(require.resolve(entry));
            const bundle = (name) => ({ mainModule: require('path').join(dist, name), mainWorker: '' });
            duckdb.createDuckDB({ mvp: bundle('duckdb-mvp.wasm'), eh: bundle('duckdb-eh.wasm') }, new duckdb.VoidLogger(), duckdb.NODE_RUNTIME)
                .then(async (bindings) => {
                    await bindings.instantiate();
                    bindings.open({});
                    voltDuckDB.bindings = bindings;
                })
                // The program runs from here, so its own exit or failure is not DuckDB's.
                .then(() => removeRunDependency('volt-duckdb'), (error) => abort('DuckDB failed to open: ' + (error && error.message ? error.message : error)));
        },

        engine() {
            return voltDuckDB.bindings;
        },

        // Arrow's type ids (apache-arrow's Type enum) as the SQL types duckdb.hpp knows.
        sqlType(type) {
            switch (type.typeId) {
                case 2: {
                    const width = { 8: 'TINYINT', 16: 'SMALLINT', 32: 'INTEGER', 64: 'BIGINT' }[type.bitWidth];
                    return type.isSigned ? width : 'U' + width;
                }
                case 3: return type.precision === 2 ? 'DOUBLE' : 'FLOAT';
                case 5: return 'VARCHAR';
                case 6: return 'BOOLEAN';
                case 7: return 'DOUBLE';
                case 12: return voltDuckDB.sqlType(type.children[0].type) + '[]';
                default: return 'VARCHAR';
            }
        },

        // A value as JSON: 64-bit integers as numbers while exact, lists as arrays, and the doubles
        // JSON has no literal for as the spellings DuckDB and strtod read.
        jsonValue(value) {
            if (value === null || value === undefined) return null;
            if (typeof value === 'bigint') {
                return value >= BigInt(Number.MIN_SAFE_INTEGER) && value <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(value) : value.toString();
            }
            if (typeof value === 'number' && !Number.isFinite(value)) {
                return Number.isNaN(value) ? 'nan' : value > 0 ? 'inf' : '-inf';
            }
            if (typeof value === 'object' && typeof value.toArray === 'function') {
                return Array.from(value.toArray(), voltDuckDB.jsonValue);
            }
            return value;
        },

        tableJson(table) {
            const columns = table.schema.fields.map((field) => ({ name: field.name, type: voltDuckDB.sqlType(field.type) }));
            const vectors = columns.map((_, index) => table.getChildAt(index));
            const rows = [];
            for (let row = 0; row < table.numRows; row++) {
                rows.push(vectors.map((vector) => voltDuckDB.jsonValue(vector.get(row))));
            }
            return { columns, rows };
        },

        reply(run) {
            let document;
            try {
                document = run();
            } catch (error) {
                document = { error: error && error.message ? error.message : String(error) };
            }
            return stringToNewUTF8(JSON.stringify(document));
        },

        quoteIdentifier(name) {
            return '"' + name.replace(/"/g, '""') + '"';
        },

        quoteLiteral(text) {
            return "'" + text.replace(/'/g, "''") + "'";
        }
    },

    $voltDuckDB__postset: 'addOnPreRun(voltDuckDB.start);',

    volt_duckdb_open__deps: ['$voltDuckDB'],
    volt_duckdb_open: () => voltDuckDB.nextDatabase++,

    volt_duckdb_connect__deps: ['$voltDuckDB'],
    volt_duckdb_connect: (database) => {
        const connection = voltDuckDB.engine().connect();
        const catalog = 'volt_' + database;
        // The engine's file system takes any other spelling of ':memory:' for a file name.
        connection.query("ATTACH IF NOT EXISTS ':memory:' AS " + catalog);
        connection.query('USE ' + catalog);
        const id = voltDuckDB.nextConnection++;
        voltDuckDB.connections[id] = connection;
        return id;
    },

    volt_duckdb_disconnect__deps: ['$voltDuckDB'],
    volt_duckdb_disconnect: (id) => {
        voltDuckDB.connections[id]?.close();
        delete voltDuckDB.connections[id];
    },

    // Drops a database once its last connection has closed, as destroying a duckdb::DuckDB does.
    volt_duckdb_close__deps: ['$voltDuckDB'],
    volt_duckdb_close: (database) => {
        const connection = voltDuckDB.engine().connect();
        try {
            connection.query('USE memory');
            connection.query('DETACH DATABASE IF EXISTS volt_' + database);
        } finally {
            connection.close();
        }
    },

    volt_duckdb_query__deps: ['$voltDuckDB', '$stringToNewUTF8', '$UTF8ToString'],
    volt_duckdb_query: (id, sql) => voltDuckDB.reply(() => voltDuckDB.tableJson(voltDuckDB.connections[id].query(UTF8ToString(sql)))),

    // Inserts rows an Appender buffered, as CSV in the table's column order, through a file of the
    // engine's own memory.
    volt_duckdb_append__deps: ['$voltDuckDB', '$stringToNewUTF8', '$UTF8ToString'],
    volt_duckdb_append: (id, table, csv, length) => voltDuckDB.reply(() => {
        const connection = voltDuckDB.connections[id];
        const name = UTF8ToString(table);
        const columns = connection.query('DESCRIBE ' + voltDuckDB.quoteIdentifier(name)).toArray()
            .map((row) => voltDuckDB.quoteLiteral(row.column_name) + ': ' + voltDuckDB.quoteLiteral(row.column_type));
        const file = 'volt-append-' + voltDuckDB.nextFile++ + '.csv';
        const engine = voltDuckDB.engine();
        engine.registerFileBuffer(file, HEAPU8.slice(csv, csv + length));
        try {
            connection.query('INSERT INTO ' + voltDuckDB.quoteIdentifier(name) + ' SELECT * FROM read_csv(' + voltDuckDB.quoteLiteral(file)
                + ", header = false, auto_detect = false, quote = '\"', escape = '\"', allow_quoted_nulls = false, columns = {"
                + columns.join(', ') + '})');
        } finally {
            engine.dropFile(file);
        }
        return { columns: [], rows: [] };
    })
});
