import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout


class CoreToolkitConan(ConanFile):
    name = "coretoolkit"
    version = "2.6.0"
    package_type = "static-library"
    license = "MIT"
    settings = "os", "arch", "compiler", "build_type"
    options = {"baseline_arch": ["off", "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "native"]}
    requires = (
        "boost/1.88.0",
        "lammpsio/[>=2.1]",
        "onetbb/2021.12.0",
        "spdlog/1.14.1",
        "nlohmann_json/3.11.3",
        "duckdb/1.4.3",
    )
    default_options = {
        "baseline_arch": "x86-64-v3",
        "hwloc/*:shared": True,
        "onetbb/*:shared": False,
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
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "dependencies/*", "cmake/*", "tools/*"

    def layout(self):
        cmake_layout(self)

    def _release_arch_flags(self):
        """Compile flags the whole VOLT stack must share, consumers included.

        Historical trap this exists to close: these flags used to be applied only
        with target_compile_options(coretoolkit PUBLIC ...) in CMakeLists.txt. That
        propagates inside one build tree, but coretoolkit ships as a Conan package,
        and PUBLIC options do not cross the package boundary — only what
        package_info() puts in cpp_info survives. Measured 2026-08-14: every plugin
        compiled at plain x86-64, and objdump found zero AVX2/FMA instructions in
        ptm_polar.cpp.o and ptm_structure_matcher.cpp.o — the two files the
        performance audit had measured 1.18-1.32x on. So verify with flags.make and
        objdump, never by reading the CMakeLists.

        -ffp-contract=fast is deliberately absent: GCC already defaults to it for
        -std=gnu++2x, so exporting it would change nothing while reading like a
        floating-point semantics change. geogram keeps -ffp-contract=off and
        -frounding-math privately, because its exact predicates depend on them.
        """
        arch = str(self.options.baseline_arch)
        if arch == "off" or str(self.settings.arch) not in ("x86_64", "x86_64v2", "x86_64v3"):
            flags = []
        elif arch == "native":
            flags = ["-march=native"]
        else:
            flags = ["-march=" + arch]
        return flags + ["-fno-math-errno", "-fno-trapping-math"]

    def generate(self):
        tc = CMakeToolchain(self)
        arch = str(self.options.baseline_arch)
        if arch == "native":
            tc.cache_variables["VOLT_ENABLE_NATIVE_OPTIMIZATIONS"] = True
        elif arch != "off":
            tc.cache_variables["VOLT_BASELINE_ARCH"] = arch
        tc.generate()
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

        if self.settings.build_type == "Release":
            arch_flags = self._release_arch_flags()
            self.cpp_info.cflags.extend(arch_flags)
            self.cpp_info.cxxflags.extend(arch_flags)

        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.append("gomp")
            self.cpp_info.cflags.append("-fopenmp")
            self.cpp_info.cxxflags.append("-fopenmp")
            self.cpp_info.exelinkflags.append("-fopenmp")
            self.cpp_info.sharedlinkflags.append("-fopenmp")
            self.cpp_info.defines.append("VOLT_HAVE_PARALLEL_DELAUNAY")
        self.cpp_info.requires = [
            "boost::headers",
            "lammpsio::lammpsio",
            "onetbb::onetbb",
            "spdlog::spdlog",
            "nlohmann_json::nlohmann_json",
            "duckdb::duckdb",
        ]
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join(self.package_folder, "lib", "cmake", "coretoolkit", "VoltPlugin.cmake")
        ])
