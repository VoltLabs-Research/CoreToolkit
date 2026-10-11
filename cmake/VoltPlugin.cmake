function(volt_pin_tbbmalloc TARGET_NAME)
    if(NOT UNIX OR APPLE OR NOT TARGET TBB::tbbmalloc)
        return()
    endif()
    set(_hints "")
    foreach(_prop INTERFACE_LINK_DIRECTORIES)
        get_target_property(_dirs TBB::tbbmalloc ${_prop})
        if(_dirs)
            list(APPEND _hints ${_dirs})
        endif()
    endforeach()
    get_target_property(_tbb_dirs TBB::tbb INTERFACE_LINK_DIRECTORIES)
    if(_tbb_dirs)
        list(APPEND _hints ${_tbb_dirs})
    endif()
    if(DEFINED onetbb_PACKAGE_FOLDER_RELEASE)
        list(APPEND _hints "${onetbb_PACKAGE_FOLDER_RELEASE}/lib")
    endif()
    if(DEFINED onetbb_TBB_tbbmalloc_LIB_DIRS_RELEASE)
        list(APPEND _hints ${onetbb_TBB_tbbmalloc_LIB_DIRS_RELEASE})
    endif()
    list(REMOVE_DUPLICATES _hints)

    unset(_loc CACHE)
    find_library(_loc NAMES tbbmalloc HINTS ${_hints} NO_DEFAULT_PATH)
    if(NOT _loc)
        message(WARNING "volt_pin_tbbmalloc: could not locate libtbbmalloc alongside "
                        "the oneTBB package (searched: ${_hints}). libtbbmalloc_proxy "
                        "may bind to a system libtbbmalloc from a different release.")
        return()
    endif()
    target_link_options(${TARGET_NAME} PRIVATE
        "LINKER:--push-state,--no-as-needed"
        "${_loc}"
        "LINKER:--pop-state"
    )
endfunction()

function(volt_add_plugin PLUGIN_NAME)
    cmake_parse_arguments(ARG
        "NO_EXECUTABLE"
        "MAIN_SOURCE;INSTALL_VENDOR_HEADERS;INSTALL_DATA"
        "SOURCES;DEPENDENCIES;PACKAGES;INCLUDE_DIRS;VENDOR_SOURCES"
        ${ARGN}
    )

    set(CMAKE_POSITION_INDEPENDENT_CODE ON PARENT_SCOPE)
    set(CMAKE_CXX_STANDARD 23 PARENT_SCOPE)
    set(CMAKE_CXX_STANDARD_REQUIRED ON PARENT_SCOPE)
    set(CMAKE_CXX_EXTENSIONS OFF PARENT_SCOPE)

    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

    if(NOT EMSCRIPTEN AND NOT TARGET TBB::tbb)
        find_package(TBB REQUIRED)
    endif()
    if(NOT TARGET Boost::headers AND NOT TARGET Boost::boost AND NOT TARGET boost::headers AND NOT TARGET boost::boost)
        find_package(Boost REQUIRED)
    endif()
    if(NOT TARGET spdlog::spdlog)
        find_package(spdlog REQUIRED)
    endif()
    if(NOT TARGET nlohmann_json::nlohmann_json)
        find_package(nlohmann_json REQUIRED)
    endif()

    foreach(_pkg IN LISTS ARG_PACKAGES)
        if(NOT TARGET ${_pkg}::${_pkg})
            find_package(${_pkg} REQUIRED)
        endif()
    endforeach()

    set(_BOOST_TARGET "")
    if(TARGET Boost::headers)
        set(_BOOST_TARGET Boost::headers)
    elseif(TARGET Boost::boost)
        set(_BOOST_TARGET Boost::boost)
    elseif(TARGET boost::headers)
        set(_BOOST_TARGET boost::headers)
    elseif(TARGET boost::boost)
        set(_BOOST_TARGET boost::boost)
    else()
        message(FATAL_ERROR "${PLUGIN_NAME} requires a Boost headers target")
    endif()

    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE _LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
        list(FILTER _LIB_SOURCES EXCLUDE REGEX ".*main\\.cpp$")
        list(FILTER _LIB_SOURCES EXCLUDE REGEX ".*plugin\\.cpp$")
    else()
        set(_LIB_SOURCES "")
        foreach(_src IN LISTS ARG_SOURCES)
            if(IS_ABSOLUTE "${_src}")
                list(APPEND _LIB_SOURCES "${_src}")
            else()
                list(APPEND _LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
            endif()
        endforeach()
    endif()

    set(_ALL_SOURCES ${_LIB_SOURCES})
    if(ARG_VENDOR_SOURCES)
        foreach(_vs IN LISTS ARG_VENDOR_SOURCES)
            if(IS_ABSOLUTE "${_vs}")
                list(APPEND _ALL_SOURCES "${_vs}")
            else()
                list(APPEND _ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_vs}")
            endif()
        endforeach()
    endif()

    add_library(${PLUGIN_NAME}_lib STATIC ${_ALL_SOURCES})
    set_target_properties(${PLUGIN_NAME}_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${PLUGIN_NAME}_lib PUBLIC cxx_std_23)

    set(_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(ARG_INCLUDE_DIRS)
        foreach(_dir IN LISTS ARG_INCLUDE_DIRS)
            if(IS_ABSOLUTE "${_dir}")
                list(APPEND _INCLUDE_DIRS "${_dir}")
            else()
                list(APPEND _INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
            endif()
        endforeach()
    endif()

    target_include_directories(${PLUGIN_NAME}_lib PUBLIC ${_INCLUDE_DIRS})
    target_precompile_headers(${PLUGIN_NAME}_lib PRIVATE <volt/core/volt.h>)

    set(_LINK_TARGETS
        ${_BOOST_TARGET}
        coretoolkit::coretoolkit
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    )
    if(NOT EMSCRIPTEN)
        list(APPEND _LINK_TARGETS TBB::tbb)
    endif()
    if(ARG_DEPENDENCIES)
        list(APPEND _LINK_TARGETS ${ARG_DEPENDENCIES})
    endif()

    target_link_libraries(${PLUGIN_NAME}_lib PUBLIC ${_LINK_TARGETS})

    if(NOT ARG_NO_EXECUTABLE)
        set(_MAIN_SRC "")
        if(ARG_MAIN_SOURCE)
            if(IS_ABSOLUTE "${ARG_MAIN_SOURCE}")
                set(_MAIN_SRC "${ARG_MAIN_SOURCE}")
            else()
                set(_MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_MAIN_SOURCE}")
            endif()
        else()
            set(_MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
            if(NOT EXISTS "${_MAIN_SRC}")
                file(GLOB _MAIN_CANDIDATES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*plugin*.cpp)
                if(_MAIN_CANDIDATES)
                    list(GET _MAIN_CANDIDATES 0 _MAIN_SRC)
                endif()
            endif()
            if(NOT EXISTS "${_MAIN_SRC}")
                file(GLOB _MAIN_CANDIDATES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*main*.cpp)
                if(_MAIN_CANDIDATES)
                    list(GET _MAIN_CANDIDATES 0 _MAIN_SRC)
                endif()
            endif()
        endif()

        if(_MAIN_SRC AND EXISTS "${_MAIN_SRC}")
            add_executable(${PLUGIN_NAME} ${_MAIN_SRC})
            target_include_directories(${PLUGIN_NAME} PRIVATE ${_INCLUDE_DIRS})
            target_precompile_headers(${PLUGIN_NAME} PRIVATE <volt/core/volt.h>)
            target_link_libraries(${PLUGIN_NAME} PRIVATE
                ${PLUGIN_NAME}_lib
            )
            if(NOT EMSCRIPTEN)
                target_link_libraries(${PLUGIN_NAME} PRIVATE TBB::tbb)
                volt_pin_tbbmalloc(${PLUGIN_NAME})
            else()
                set_target_properties(${PLUGIN_NAME} PROPERTIES SUFFIX ".js")
                target_compile_options(${PLUGIN_NAME} PRIVATE -fexceptions)
                target_link_options(${PLUGIN_NAME} PRIVATE
                    "SHELL:-sALLOW_MEMORY_GROWTH=1"
                    "SHELL:-sMAXIMUM_MEMORY=4GB"
                    "SHELL:-sEXIT_RUNTIME=0"
                    "SHELL:-sMODULARIZE=0"
                    "SHELL:-sENVIRONMENT=web,node"
                    "SHELL:-sFORCE_FILESYSTEM=1"
                    "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
                    "SHELL:-sEXPORTED_RUNTIME_METHODS=['callMain','FS']"
                )
            endif()
            install(TARGETS ${PLUGIN_NAME} DESTINATION bin)
            if(EMSCRIPTEN)
                install(FILES "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/${PLUGIN_NAME}.wasm"
                    DESTINATION bin OPTIONAL)
            endif()
        endif()
    endif()

    install(TARGETS ${PLUGIN_NAME}_lib DESTINATION lib)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)

    if(ARG_INSTALL_VENDOR_HEADERS)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_INSTALL_VENDOR_HEADERS}/
            DESTINATION include FILES_MATCHING PATTERN "*.h")
    endif()

    if(ARG_INSTALL_DATA)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_INSTALL_DATA}/
            DESTINATION share/volt/${ARG_INSTALL_DATA})
    endif()
endfunction()
