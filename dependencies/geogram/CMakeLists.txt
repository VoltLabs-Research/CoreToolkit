cmake_minimum_required(VERSION 3.20)
project(geogram)

# NOTE: coretoolkit does NOT add_subdirectory() this file. The geogram target that
# actually gets built is declared inline in ../../CMakeLists.txt, so anything set
# here has no effect on a normal coretoolkit build. This file is kept only so the
# amalgamation can be built standalone, and is deliberately kept in sync with the
# real target — if you change one, change the other.
#
# Historical trap: this file used to define GARGANTUA=1 (64-bit index_t), which
# never applied to the real build and so was silently misleading about what
# production actually compiles. Do not reintroduce it without measuring: 64-bit
# indices double the size of the tetrahedron connectivity — the largest data
# structure in the DXA pipeline — and are only needed above ~4e9 elements.

file(GLOB_RECURSE GEOGRAM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(geogram STATIC ${GEOGRAM_SOURCES})

target_compile_definitions(geogram PUBLIC GEO_STATIC_LIBS=1)
target_compile_options(geogram PRIVATE
    "-frounding-math" "-ffp-contract=off"
    $<$<CXX_COMPILER_ID:MSVC>:/wd4251;/wd4275;/wd4244;/wd4334>
)
target_include_directories(geogram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(geogram PUBLIC Threads::Threads Boost::boost)

# See ../../CMakeLists.txt for why the macro and OpenMP are required together:
# without GEOGRAM_WITH_PDEL the backend is never registered, and without OpenMP
# there is no thread manager, so PDEL hands work to threads nothing schedules.
option(VOLT_ENABLE_PARALLEL_DELAUNAY "Build geogram's ParallelDelaunay3d (PDEL) backend" ON)
if(VOLT_ENABLE_PARALLEL_DELAUNAY)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_compile_definitions(geogram PUBLIC GEOGRAM_WITH_PDEL VOLT_HAVE_PARALLEL_DELAUNAY)
        target_link_libraries(geogram PUBLIC OpenMP::OpenMP_CXX)
    endif()
endif()