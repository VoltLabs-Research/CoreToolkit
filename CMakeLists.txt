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

project(coretoolkit VERSION 1.0.0 LANGUAGES CXX)

# Force PIC globally so that static libs can be linked into .so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type & optimization
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

# External packages
find_package(Threads REQUIRED)
find_package(TBB REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED)

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
target_include_directories(geogram PUBLIC
	${CMAKE_SOURCE_DIR}/dependencies/geogram
	${Boost_INCLUDE_DIRS}
)
if(VOLT_BOOST_TARGET)
	target_link_libraries(geogram PUBLIC ${VOLT_BOOST_TARGET})
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
	mwm_csp
	geogram
)

# Install main library and embedded libs
install(TARGETS ${PROJECT_NAME} mwm_csp geogram DESTINATION lib)

# HPC optimization flags for Release builds (propagate to all consumers via PUBLIC)
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:
		-O3 -march=native -fno-math-errno -fno-trapping-math -ffp-contract=fast
	>
)
# Keep strict IEEE on geometry-critical code — NOT using -ffast-math globally
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/mwm_csp/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/geogram/ DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/fast_float/ DESTINATION include FILES_MATCHING PATTERN "*.h")
