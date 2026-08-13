#
# Copyright (C) Rodolfo Herrera Hernandez. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(coretoolkit VERSION 2.0.0 LANGUAGES CXX)

# Force PIC globally so that static libs can be linked into .so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VOLT_ENABLE_NATIVE_OPTIMIZATIONS "Tune for the build machine's exact CPU (-march=native). Not portable." OFF)

# Baseline ISA for Release builds.
#
# The previous default was -mtune=generic, which leaves the *instruction set* at
# plain x86-64 (SSE2 only): no AVX2, no FMA. Measured on the vendored PTM library
# (256k-atom FCC Al, 16 threads), moving to an AVX2+FMA baseline is worth
# 1.18-1.32x depending on the stage — the code is branch-heavy and scalar, so the
# autovectoriser has limited room, but the gain is free.
#
# x86-64-v3 == AVX2 + FMA + BMI2 + MOVBE, i.e. Haswell (2013) / Excavator (2015)
# and newer. It keeps the binary portable across every machine we ship to, unlike
# -march=native which bakes in the build host's exact microarchitecture.
#
# Set VOLT_BASELINE_ARCH to something else (e.g. x86-64-v2 for pre-2013 hardware,
# or x86-64 to restore the old behaviour) if a deployment target requires it.
set(VOLT_BASELINE_ARCH "x86-64-v3" CACHE STRING
	"Baseline -march for Release builds (x86-64, x86-64-v2, x86-64-v3, x86-64-v4)")

if(VOLT_ENABLE_NATIVE_OPTIMIZATIONS)
	set(VOLT_RELEASE_ARCH_FLAGS -march=native)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
	set(VOLT_RELEASE_ARCH_FLAGS -march=${VOLT_BASELINE_ARCH})
else()
	# Non-x86 (arm64 etc): no -march spelling in common, just tune generically.
	set(VOLT_RELEASE_ARCH_FLAGS -mtune=generic)
endif()

# Build type & optimization
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

# External packages
find_package(Threads REQUIRED)
find_package(TBB REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED)
find_package(duckdb REQUIRED)
find_package(lammpsio REQUIRED)

set(VOLT_BOOST_TARGET "")
if(TARGET Boost::headers)
	set(VOLT_BOOST_TARGET Boost::headers)
elseif(TARGET Boost::boost)
	set(VOLT_BOOST_TARGET Boost::boost)
elseif(TARGET boost::headers)
	set(VOLT_BOOST_TARGET boost::headers)
elseif(TARGET boost::boost)
	set(VOLT_BOOST_TARGET boost::boost)
endif()

# MWM_CSP library (for centrosymmetry matching)
file(GLOB MWM_CSP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/dependencies/mwm_csp/*.cpp)
add_library(mwm_csp STATIC ${MWM_CSP_SOURCES})
target_include_directories(mwm_csp PUBLIC ${CMAKE_SOURCE_DIR}/dependencies/mwm_csp)

# Geogram (Delaunay tessellation)
set(GEOGRAM_SOURCES ${CMAKE_SOURCE_DIR}/dependencies/geogram/Delaunay_psm.cpp)
add_library(geogram STATIC ${GEOGRAM_SOURCES})
# Delaunay_psm.h defaults to DLL import/export unless GEO_STATIC_LIBS is set.
# We build geogram as a static library, and MSVC rejects definitions marked
# as dllimport (for example Periodic::reorder_instances).
target_compile_definitions(geogram PUBLIC GEO_STATIC_LIBS)
target_include_directories(geogram PUBLIC
	${CMAKE_SOURCE_DIR}/dependencies/geogram
	${Boost_INCLUDE_DIRS}
)
if(VOLT_BOOST_TARGET)
	target_link_libraries(geogram PUBLIC ${VOLT_BOOST_TARGET})
endif()

# geogram needs -frounding-math: its exact geometric predicates rely on the FPU
# not being allowed to assume a fixed rounding mode. Without it the compiler may
# reorder/contract arithmetic in ways that break the orientation/insphere tests.
target_compile_options(geogram PRIVATE -frounding-math -ffp-contract=off)

# Parallel Delaunay (PDEL).
#
# ParallelDelaunay3d is guarded behind GEOGRAM_WITH_PDEL in Delaunay_psm.cpp; if
# the macro is absent the "PDEL" backend is never even registered with the factory
# (see the geo_register_Delaunay_creator calls). Separately, the *only* real thread
# manager in the amalgamation is OMPThreadManager, which lives inside an
# `#ifdef GEO_OPENMP`. Building geogram without OpenMP therefore leaves PDEL
# handing work to threads that nothing ever schedules — which is exactly what the
# "PDEL hangs on a 160-core EPYC" symptom (threads spinning at 99.9% CPU, or a
# single sleeping thread) looks like from the outside.
#
# With both enabled, PDEL produces a tessellation with a cell count identical to
# the serial BDEL backend and is 2.4-3.0x faster (measured, 256k-864k atoms,
# 8 cores/16 threads). See DelaunayTessellation::generateTessellation for the
# runtime thread cap.
option(VOLT_ENABLE_PARALLEL_DELAUNAY "Build geogram's ParallelDelaunay3d (PDEL) backend" ON)
if(VOLT_ENABLE_PARALLEL_DELAUNAY)
	find_package(OpenMP)
	if(OpenMP_CXX_FOUND)
		target_compile_definitions(geogram PUBLIC GEOGRAM_WITH_PDEL VOLT_HAVE_PARALLEL_DELAUNAY)
		target_link_libraries(geogram PUBLIC OpenMP::OpenMP_CXX)
		message(STATUS "coretoolkit: geogram parallel Delaunay (PDEL) enabled")
	else()
		message(WARNING
			"coretoolkit: OpenMP not found — geogram falls back to the serial BDEL "
			"Delaunay backend. PDEL without OpenMP would deadlock, so it stays off.")
	endif()
endif()

file(GLOB_RECURSE LIBRARY_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_library(${PROJECT_NAME} STATIC ${LIBRARY_SOURCES})
target_precompile_headers(${PROJECT_NAME} PRIVATE <volt/core/volt.h>)
target_include_directories(${PROJECT_NAME} PUBLIC
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/dependencies
	${Boost_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/dependencies/fast_float
	${CMAKE_SOURCE_DIR}/dependencies/mwm_csp
	${CMAKE_SOURCE_DIR}/dependencies/geogram
)

target_link_libraries(${PROJECT_NAME} PUBLIC
	Threads::Threads
	TBB::tbb
	spdlog::spdlog
	nlohmann_json::nlohmann_json
	${VOLT_BOOST_TARGET}
	duckdb::duckdb
	lammpsio::lammpsio
	mwm_csp
	geogram
)

# Install main library and embedded libs
install(TARGETS ${PROJECT_NAME} mwm_csp geogram DESTINATION lib)

# volt-dump-transform — daemon-side LAMMPS dump mutator (slice / select / merge).
# Links the coretoolkit target so it inherits the LAMMPS parser, the expression
# engine and duckdb (Parquet reader) transitively.
add_executable(volt-dump-transform tools/dump_transform.cpp)
target_link_libraries(volt-dump-transform PRIVATE ${PROJECT_NAME})
# The daemon ships this binary inside its container and runs it on whatever Linux
# the user enrolled (the dev daemon runs in an Ubuntu 24.04 / glibc 2.39 image).
# Two portability hazards: (1) a binary built against a newer host glibc fails to
# load with `GLIBC_x.yy not found` — fixed by BUILDING the binary in a glibc-2.39
# image (see tools/build-dump-transform.sh, which compiles inside ubuntu:24.04);
# (2) libstdc++/libgcc version skew — fixed by linking those statically. oneTBB
# (conan) is shared-only so the binary keeps a libtbb.so.12 dependency, which the
# daemon image satisfies (TBB is an apt/runtime lib); RPATH $ORIGIN lets a
# co-located libtbb.so.12 win if present.
if(UNIX AND NOT APPLE)
    target_link_options(volt-dump-transform PRIVATE
        -static-libstdc++ -static-libgcc
        "LINKER:-rpath,$ORIGIN")
endif()
install(TARGETS volt-dump-transform DESTINATION bin)

# HPC optimization flags for Release builds (propagate to all consumers via PUBLIC).
# VOLT_RELEASE_ARCH_FLAGS is set once near the top of this file.
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:
		-O3 ${VOLT_RELEASE_ARCH_FLAGS} -fno-math-errno -fno-trapping-math -ffp-contract=fast
	>
)
# Keep strict IEEE on geometry-critical code — NOT using -ffast-math globally
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/mwm_csp/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/geogram/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/fast_float/ DESTINATION include FILES_MATCHING PATTERN "*.h")

install(FILES ${CMAKE_SOURCE_DIR}/cmake/VoltPlugin.cmake DESTINATION lib/cmake/coretoolkit)
