// The shell of a program built for Node (cmake/VoltPlugin.cmake's wasm builds): it runs as
// `node <program>` or as an executable, by the #!/usr/bin/env node line node-program.shebang.js puts
// first. Its exit ends the process, as a C program's does, whether or not the event loop would drain
// (a runtime it runs on may never report it idle), and its standard streams are terminals only when
// they are: NODERAWFS takes them for terminals, and programs check (zstd will not write to one).
//
// Emscripten calls onExit when main returns or exit() is called. It no longer reads Module.quit (not
// in INCOMING_MODULE_JS_API), whose Node default only sets process.exitCode. An uncaught exception
// still ends the process as Node ends one.
Module['onExit'] = (status) => process.exit(status);
Module['preRun'] = [].concat(Module['preRun'] || [], () => {
    // The program's executable is the program file, where it finds what it installs beside it
    // (share/), as a native build finds its own through /proc/self/exe.
    const program = require('path').resolve(process.argv[1]);
    const readlink = FS.readlink;
    FS.readlink = (path, ...rest) => (path === '/proc/self/exe' ? program : readlink(path, ...rest));
    addOnPreMain(() => {
        const tty = require('tty');
        for (let fd = 0; fd < 3; fd++) {
            FS.getStream(fd).tty = tty.isatty(fd);
        }
    });
});
