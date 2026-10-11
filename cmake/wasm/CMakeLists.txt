cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(volt-wasm-superbuild LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "cmake/wasm is an Emscripten superbuild. Configure with emcmake.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

add_compile_options(-fexceptions)
add_link_options("SHELL:-sDISABLE_EXCEPTION_CATCHING=0")

set(VOLT_PLUGIN_SOURCE "" CACHE PATH "Plugin source tree")
set(VOLT_CORETOOLKIT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "CoreToolkit source tree")
set(VOLT_LAMMPSIO_SOURCE "" CACHE PATH "LammpsIO source tree")
set(VOLT_DEPENDENCY_DIRS "" CACHE STRING "Semicolon-separated sibling plugin source trees")

if(NOT VOLT_PLUGIN_SOURCE)
    message(FATAL_ERROR "VOLT_PLUGIN_SOURCE is required")
endif()
if(NOT VOLT_LAMMPSIO_SOURCE)
    message(FATAL_ERROR "VOLT_LAMMPSIO_SOURCE is required")
endif()

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(spdlog)

set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(yaml-cpp)
if(NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()

set(VOLT_BOOST_ROOT "" CACHE PATH "Pre-extracted Boost headers root (contains boost/)")
if(NOT VOLT_BOOST_ROOT)
    FetchContent_Declare(boost_headers_src
        URL https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz
        URL_HASH SHA256=3621533e820dcab1e8012afd583c0c73cf0f77694952b81352bf38c1488f9cb4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(boost_headers_src)
    if(NOT boost_headers_src_POPULATED)
        FetchContent_Populate(boost_headers_src)
    endif()
    set(VOLT_BOOST_ROOT "${boost_headers_src_SOURCE_DIR}")
endif()
if(NOT EXISTS "${VOLT_BOOST_ROOT}/boost/dynamic_bitset.hpp")
    file(GLOB_RECURSE _boost_probe "${VOLT_BOOST_ROOT}/*/boost/dynamic_bitset.hpp")
    if(_boost_probe)
        list(GET _boost_probe 0 _boost_header)
        get_filename_component(_boost_include "${_boost_header}" DIRECTORY)
        get_filename_component(VOLT_BOOST_ROOT "${_boost_include}" DIRECTORY)
    endif()
endif()
if(NOT EXISTS "${VOLT_BOOST_ROOT}/boost/dynamic_bitset.hpp")
    message(FATAL_ERROR "Boost headers not found under ${VOLT_BOOST_ROOT}")
endif()
add_library(volt_boost_headers INTERFACE)
target_include_directories(volt_boost_headers INTERFACE "${VOLT_BOOST_ROOT}")
add_library(Boost::headers ALIAS volt_boost_headers)
if(NOT TARGET Boost::boost)
    add_library(Boost::boost ALIAS volt_boost_headers)
endif()

add_subdirectory("${VOLT_LAMMPSIO_SOURCE}" "${CMAKE_BINARY_DIR}/_deps/lammpsio")
if(NOT TARGET lammpsio::lammpsio)
    add_library(lammpsio::lammpsio ALIAS lammpsio)
endif()

add_subdirectory("${VOLT_CORETOOLKIT_SOURCE}" "${CMAKE_BINARY_DIR}/_deps/coretoolkit")
if(NOT TARGET coretoolkit::coretoolkit)
    add_library(coretoolkit::coretoolkit ALIAS coretoolkit)
endif()
include("${VOLT_CORETOOLKIT_SOURCE}/cmake/VoltPlugin.cmake")

add_library(TBB::tbb INTERFACE IMPORTED)
target_include_directories(TBB::tbb INTERFACE "${VOLT_CORETOOLKIT_SOURCE}/compat/wasm")
add_library(duckdb::duckdb INTERFACE IMPORTED)
target_include_directories(duckdb::duckdb INTERFACE "${VOLT_CORETOOLKIT_SOURCE}/compat/wasm")

function(volt_alias_exported_target source_dir)
    get_filename_component(_leaf "${source_dir}" NAME)
    string(TOLOWER "${_leaf}" _key)
    if(_key STREQUAL "structureidentification" OR _key STREQUAL "structure-identification")
        if(NOT TARGET structure-identification::structure-identification AND TARGET structure-identification)
            add_library(structure-identification::structure-identification ALIAS structure-identification)
        endif()
    elseif(_key STREQUAL "commonneighboranalysis" OR _key STREQUAL "adaptive-common-neighbor-analysis")
        if(NOT TARGET common-neighbor-analysis::common-neighbor-analysis AND TARGET common-neighbor-analysis_lib)
            add_library(common-neighbor-analysis::common-neighbor-analysis ALIAS common-neighbor-analysis_lib)
        endif()
    elseif(_key STREQUAL "polyhedraltemplatematching" OR _key STREQUAL "polyhedral-template-matching")
        if(NOT TARGET polyhedral-template-matching::polyhedral-template-matching AND TARGET polyhedral-template-matching_lib)
            add_library(polyhedral-template-matching::polyhedral-template-matching ALIAS polyhedral-template-matching_lib)
        endif()
    elseif(_key STREQUAL "opendxa")
        if(NOT TARGET opendxa::opendxa AND TARGET opendxa_lib)
            add_library(opendxa::opendxa ALIAS opendxa_lib)
        endif()
    endif()
endfunction()

foreach(_dep IN LISTS VOLT_DEPENDENCY_DIRS)
    if(NOT _dep)
        continue()
    endif()
    get_filename_component(_dep_name "${_dep}" NAME)
    add_subdirectory("${_dep}" "${CMAKE_BINARY_DIR}/_deps/${_dep_name}")
    volt_alias_exported_target("${_dep}")
endforeach()

add_subdirectory("${VOLT_PLUGIN_SOURCE}" "${CMAKE_BINARY_DIR}/_plugin")
