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
        "arrow/18.1.0",
    )
    default_options = {
        "hwloc/*:shared": True,
        "onetbb/*:shared": False,
        # We only ever write Parquet (arrow/api.h, arrow/io/file.h,
        # parquet/arrow/writer.h). Keep Arrow's build minimal: there is no
        # ConanCenter binary for a parquet-enabled Arrow, so it always compiles
        # from source here and every extra module is dead weight on the build
        # clock. These mirror the recipe defaults (so the package_id is
        # unchanged); they are pinned explicitly to guard against default drift.
        "arrow/*:parquet": True,
        "arrow/*:with_zstd": True,
        "arrow/*:compute": False,
        "arrow/*:acero": False,
        "arrow/*:dataset_modules": False,
        "arrow/*:gandiva": False,
        "arrow/*:with_flight_rpc": False,
        "arrow/*:with_csv": False,
        "arrow/*:with_json": False,
        "arrow/*:with_orc": False,
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
            "arrow::libarrow",
            "arrow::libparquet",
        ]
        self.cpp_info.set_property("cmake_build_modules", [
            os.path.join(self.package_folder, "lib", "cmake", "coretoolkit", "VoltPlugin.cmake")
        ])
