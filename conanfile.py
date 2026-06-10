import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class CoreToolkitConan(ConanFile):
    name = "coretoolkit"
    version = "2.0.0"
    package_type = "static-library"
    license = "MIT"
    settings = "os", "arch", "compiler", "build_type"
    requires = (
        "boost/1.88.0",
        "onetbb/2021.12.0",
        "spdlog/1.14.1",
        "nlohmann_json/3.11.3",
        "duckdb/1.4.3",
    )
    default_options = {
        "hwloc/*:shared": True,
        "onetbb/*:shared": False,
        # NOTE: boost/*:without_stacktrace is intentionally NOT set here. It is a
        # Linux-only fix and is applied per-OS by the build entrypoints instead
        # (CI conan_args gated on runner.os, Dockerfile.build, scripts/install*.sh
        # — all Linux). Reason: on Linux, boost 1.88 builds
        # libboost_stacktrace_from_exception.a, which interposes
        # __cxa_allocate_exception; linking a plugin with -static-libstdc++ then
        # fails with "multiple definition of __cxa_allocate_exception" against
        # libstdc++.a, so we drop the stacktrace libs (we only use Boost headers).
        # But ConanCenter ships NO prebuilt boost binary with without_stacktrace=True
        # for ANY OS — setting it globally forced boost to compile from source on
        # macOS and Windows too. Keeping the default everywhere except Linux lets
        # macOS/Windows download the prebuilt boost binary.
        # DuckDB is our Parquet engine. Unlike Arrow (no ConanCenter binary for a
        # parquet build -> ~35 min source compile on every OS, and a hard MSVC
        # build failure on Windows), DuckDB ships prebuilt binaries for every CI
        # target, so it downloads instead of compiling. We only need a static lib
        # with the built-in Parquet writer; every optional extension is off to
        # keep the package minimal (these mirror the recipe defaults).
        "duckdb/*:shared": False,
        "duckdb/*:with_parquet": True,
        "duckdb/*:with_httpfs": False,
        "duckdb/*:with_json": False,
        "duckdb/*:with_icu": False,
        "duckdb/*:with_tpch": False,
        "duckdb/*:with_tpcds": False,
        "duckdb/*:with_fts": False,
        "duckdb/*:with_inet": False,
        "duckdb/*:with_excel": False,
        "duckdb/*:with_autocomplete": False,
        "duckdb/*:with_visualizer": False,
        "duckdb/*:with_sqlsmith": False,
        "duckdb/*:with_shell": False,
    }
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "dependencies/*", "cmake/*"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "coretoolkit::coretoolkit")
        self.cpp_info.libs = ["coretoolkit", "mwm_csp", "geogram"]
        self.cpp_info.defines = ["GEO_STATIC_LIBS"]
        self.cpp_info.requires = [
            "boost::headers",
            "onetbb::onetbb",
            "spdlog::spdlog",
            "nlohmann_json::nlohmann_json",
            "duckdb::duckdb",
        ]
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join(self.package_folder, "lib", "cmake", "coretoolkit", "VoltPlugin.cmake")
        ])
