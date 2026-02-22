cmake_minimum_required(VERSION 3.13)

project(lace
    VERSION 2.0.3
    DESCRIPTION "Lace, a work-stealing framework for multi-core fork-join parallelism"
    HOMEPAGE_URL "https://github.com/trolando/lace"
    LANGUAGES C CXX
)

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/cmake")

option(LACE_PIE_TIMES "Let Lace record Pie times" OFF)
option(LACE_COUNT_TASKS "Let Lace record the number of tasks" OFF)
option(LACE_COUNT_STEALS "Let Lace count #steals and #leaps" OFF)
option(LACE_COUNT_SPLITS "Let Lace count #splits" OFF)
option(LACE_BACKOFF "Let idle threads sleep if there is no work" ON)
option(LACE_USE_HWLOC "Let Lace pin threads/memory using libhwloc" OFF)
option(LACE_USE_MMAP "Let Lace use mmap to allocate memory" ON)

include(CheckSymbolExists)
check_symbol_exists(sched_getaffinity "sched.h" HAVE_SCHED_GETAFFINITY)
if(HAVE_SCHED_GETAFFINITY)
    target_compile_definitions(lace PRIVATE HAVE_SCHED_GETAFFINITY=1)
    target_compile_definitions(lace32 PRIVATE HAVE_SCHED_GETAFFINITY=1)
    target_compile_definitions(lace128 PRIVATE HAVE_SCHED_GETAFFINITY=1)
endif()

if(LACE_USE_MMAP AND NOT MSVC)
    include(CheckSymbolExists)
    check_symbol_exists(mmap "sys/mman.h" HAVE_MMAP)
    if(NOT HAVE_MMAP)
        message(WARNING " mmap not found: disabling mmap support")
	    set(LACE_USE_MMAP OFF)
    endif()
endif()

add_library(lace STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lace.h
)
add_library(lace32 STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lace32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lace32.h
)
add_library(lace128 STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lace128.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lace128.h
)

add_library(lace::lace ALIAS lace)
add_library(lace::lace32 ALIAS lace32)
add_library(lace::lace128 ALIAS lace128)

target_compile_features(lace PUBLIC c_std_11)
target_compile_features(lace32 PUBLIC c_std_11)
target_compile_features(lace128 PUBLIC c_std_11)

target_include_directories(lace
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_include_directories(lace32
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_include_directories(lace128
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)

option(LACE_NATIVE_OPT "Enable -march=native for compiler optimizations" ON)

option(LACE_ENABLE_PIC "Build Lace with -fPIC (e.g. for linking into shared libs)" OFF)
set_target_properties(lace PROPERTIES POSITION_INDEPENDENT_CODE ${LACE_ENABLE_PIC})
set_target_properties(lace32 PROPERTIES POSITION_INDEPENDENT_CODE ${LACE_ENABLE_PIC})
set_target_properties(lace128 PROPERTIES POSITION_INDEPENDENT_CODE ${LACE_ENABLE_PIC})

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lace PRIVATE
        -pipe
        $<$<CONFIG:Debug>:-O0 -Wall -Wextra -Wpedantic>
        $<$<BOOL:${LACE_NATIVE_OPT}>:-march=native>
    )

    target_compile_options(lace32 PRIVATE
        -pipe
        $<$<CONFIG:Debug>:-O0 -Wall -Wextra -Wpedantic>
        $<$<BOOL:${LACE_NATIVE_OPT}>:-march=native>
    )

    target_compile_options(lace128 PRIVATE
        -pipe
        $<$<CONFIG:Debug>:-O0 -Wall -Wextra -Wpedantic>
        $<$<BOOL:${LACE_NATIVE_OPT}>:-march=native>
    )
elseif(MSVC)
    target_compile_options(lace PUBLIC
        /Zc:__STDC__
        /experimental:c11atomics
    )
    target_compile_options(lace32 PUBLIC
        /Zc:__STDC__
        /experimental:c11atomics
    )
    target_compile_options(lace128 PUBLIC
        /Zc:__STDC__
        /experimental:c11atomics
    )
endif()

if(NOT MSVC)
    find_package(Threads REQUIRED)
    target_link_libraries(lace PUBLIC Threads::Threads)
    target_link_libraries(lace32 PUBLIC Threads::Threads)
    target_link_libraries(lace128 PUBLIC Threads::Threads)
endif()

if(LACE_USE_HWLOC)
    find_package(HWLOC REQUIRED)
    target_link_libraries(lace PUBLIC HWLOC::hwloc)
    target_link_libraries(lace32 PUBLIC HWLOC::hwloc)
    target_link_libraries(lace128 PUBLIC HWLOC::hwloc)
endif()

set_target_properties(lace PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
)

set_target_properties(lace32 PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
)

set_target_properties(lace128 PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
)

configure_file(src/lace_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/lace_config.h @ONLY)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    option(LACE_BUILD_TESTS "Build and run Lace tests" ON)
    option(LACE_BUILD_BENCHMARKS "Build Lace benchmark programs" ON)

    if(LACE_BUILD_BENCHMARKS)
        add_subdirectory(benchmarks)
    endif()

    if(LACE_BUILD_TESTS)
        # Add test directory and tests
        include(CTest)
        enable_testing()
        add_subdirectory(test)
    endif()
endif()

include(GNUInstallDirs)

install(TARGETS lace lace32 lace128
    EXPORT lace-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lace.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lace32.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lace128.h"
    "${CMAKE_CURRENT_BINARY_DIR}/lace_config.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT lace-targets
    FILE lace-targets.cmake
    NAMESPACE lace::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lace
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lace-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/lace-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lace
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/lace-config-version.cmake
    VERSION ${LACE_VERSION}
    COMPATIBILITY SameMinorVersion
)

set(_PC_EXTRA_CFLAGS "")
set(_PC_EXTRA_LIBS "")

if(LACE_USE_HWLOC)
    foreach(inc ${HWLOC_INCLUDE_DIRS})
        list(APPEND _PC_EXTRA_CFLAGS "-I${inc}")
    endforeach()
    foreach(lib ${HWLOC_LIBRARIES})
        get_filename_component(libdir ${lib} DIRECTORY)
        get_filename_component(libname ${lib} NAME_WE)
        string(REGEX REPLACE "^lib" "" libname "${libname}")
        list(APPEND _PC_EXTRA_LIBS "-L${libdir}" "-l${libname}")
    endforeach()
endif()

# Join lists into space-separated strings
string(JOIN " " PC_EXTRA_CFLAGS ${_PC_EXTRA_CFLAGS})
string(JOIN " " PC_EXTRA_LIBS ${_PC_EXTRA_LIBS})

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/lace-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/lace-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lace
)

if(NOT MSVC)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/lace.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/lace.pc" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/lace32.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/lace32.pc" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/lace128.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/lace128.pc" @ONLY)

    message(STATUS "Generated EXTRA_LIBS for .pc = \"${PC_EXTRA_LIBS}\"")

    install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/lace.pc
        ${CMAKE_CURRENT_BINARY_DIR}/lace32.pc
        ${CMAKE_CURRENT_BINARY_DIR}/lace128.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )   
endif()

export(EXPORT lace-targets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/cmake/lace-targets.cmake
    NAMESPACE lace::
)
