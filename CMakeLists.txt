cmake_minimum_required(VERSION 3.13)

project(lace
    VERSION 2.1.0
    DESCRIPTION "Lace, a work-stealing framework for multi-core fork-join parallelism"
    HOMEPAGE_URL "https://github.com/trolando/lace"
    LANGUAGES C
)

# if we ever go to 3.21, use PROJECT_IS_TOP_LEVEL
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" lace_is_top_level)

if(lace_is_top_level AND NOT CMAKE_CONFIGURATION_TYPES)
    # Default to Release when building as top project
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/cmake")

option(LACE_PIE_TIMES    "Let Lace record Pie times"                                 OFF)
option(LACE_COUNT_TASKS  "Let Lace record the number of tasks"                       OFF)
option(LACE_COUNT_STEALS "Let Lace count #steals and #leaps"                         OFF)
option(LACE_COUNT_SPLITS "Let Lace count #splits"                                    OFF)
option(LACE_USE_HWLOC    "Let Lace pin threads/memory using libhwloc"                OFF)
option(LACE_USE_MMAP     "Let Lace use mmap to allocate virtual memory"              ON)
option(LACE_BACKOFF      "Let idle threads sleep if there is no work"                ON)
option(LACE_NATIVE_OPT   "Enable -march=native for compiler optimizations"           ON)
option(LACE_ENABLE_PIC   "Build Lace with -fPIC (e.g. for linking into shared libs)" OFF)

if(lace_is_top_level)
    option(LACE_BUILD_TESTS      "Build and run Lace tests"                          ON)
    option(LACE_BUILD_BENCHMARKS "Build Lace benchmark programs"                     ON)
    option(LACE_SANITIZE_ADDRESS "Enable AddressSanitizer"                           OFF)
    option(LACE_SANITIZE_THREAD  "Enable ThreadSanitizer"                            OFF)
    option(LACE_SANITIZE_UB      "Enable UndefinedBehaviorSanitizer"                 OFF)
endif()

include(CheckSymbolExists)
check_symbol_exists(sched_getaffinity "sched.h" HAVE_SCHED_GETAFFINITY)
check_symbol_exists(mmap "sys/mman.h" HAVE_MMAP)

if(LACE_USE_MMAP AND NOT WIN32 AND NOT HAVE_MMAP)
    # Note: in WIN32, we can always use VirtualAlloc so we do not check then
    message(WARNING " mmap not found: disabling mmap support")
    set(LACE_USE_MMAP OFF)
endif()

if(LACE_SANITIZE_ADDRESS AND LACE_SANITIZE_THREAD)
   message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be used together")
endif()

set(lace_sanitizer_flags "")
if(lace_is_top_level)
    if(LACE_SANITIZE_ADDRESS)
        list(APPEND lace_sanitizer_flags -fsanitize=address -fno-omit-frame-pointer)
    endif()
    if(LACE_SANITIZE_THREAD)
        list(APPEND lace_sanitizer_flags -fsanitize=thread)
    endif()
    if(LACE_SANITIZE_UB)
        list(APPEND lace_sanitizer_flags -fsanitize=undefined)
    endif()
endif()

if(lace_sanitizer_flags)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(${lace_sanitizer_flags})
        add_link_options(${lace_sanitizer_flags})
    else()
        message(WARNING "Sanitizers requested but not supported by compiler '${CMAKE_C_COMPILER_ID}' -- ignoring")
    endif()
endif()

if(NOT MSVC)
    find_package(Threads REQUIRED)
endif()

if(LACE_USE_HWLOC)
    find_package(HWLOC REQUIRED)
endif()

foreach(_target lace lace32 lace128)
    if(_target STREQUAL "lace")
        set(_src ${CMAKE_CURRENT_SOURCE_DIR}/src/lace.c ${CMAKE_CURRENT_SOURCE_DIR}/src/lace.h)
    elseif(_target STREQUAL "lace32")
        set(_src ${CMAKE_CURRENT_SOURCE_DIR}/src/lace32.c ${CMAKE_CURRENT_SOURCE_DIR}/src/lace32.h)
    else()
        set(_src ${CMAKE_CURRENT_SOURCE_DIR}/src/lace128.c ${CMAKE_CURRENT_SOURCE_DIR}/src/lace128.h)
    endif()

    add_library(${_target} STATIC ${_src})
    add_library(lace::${_target} ALIAS ${_target})

    target_compile_features(${_target} PUBLIC c_std_11)

    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:include>
    )

    set_target_properties(${_target} PROPERTIES POSITION_INDEPENDENT_CODE ${LACE_ENABLE_PIC})

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${_target} PRIVATE
            -pipe
            $<$<CONFIG:Debug>:-O0 -Wall -Wextra -Wpedantic>
            $<$<BOOL:${LACE_NATIVE_OPT}>:-march=native>
        )
    elseif(MSVC)
        target_compile_options(${_target} PUBLIC
            /Zc:__STDC__
            /experimental:c11atomics
        )
    endif()

    if(NOT MSVC)
        target_link_libraries(${_target} PUBLIC Threads::Threads)
    endif()

    if(LACE_USE_HWLOC)
        target_link_libraries(${_target} PUBLIC HWLOC::hwloc)
    endif()

    set_target_properties(${_target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
endforeach()

configure_file(src/lace_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/lace_config.h @ONLY)

if(lace_is_top_level)
    enable_language(CXX)

    if(LACE_BUILD_BENCHMARKS)
        add_subdirectory(benchmarks)
    endif()

    if(LACE_BUILD_TESTS)
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
    VERSION ${PROJECT_VERSION}
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

# Summary
message(STATUS "")
message(STATUS "Lace ${PROJECT_VERSION} configuration summary:")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Top-level project : ${lace_is_top_level}")
message(STATUS "  Use mmap          : ${LACE_USE_MMAP}")
message(STATUS "  Use HWLOC         : ${LACE_USE_HWLOC}")
message(STATUS "  Use -march=native : ${LACE_NATIVE_OPT}")
message(STATUS "  Use -fPIC         : ${LACE_ENABLE_PIC}")
message(STATUS "  Backoff when idle : ${LACE_BACKOFF}")
message(STATUS "  Count tasks       : ${LACE_COUNT_TASKS}")
message(STATUS "  Count steals      : ${LACE_COUNT_STEALS}")
message(STATUS "  Count splits      : ${LACE_COUNT_SPLITS}")
message(STATUS "  Record PIE times  : ${LACE_PIE_TIMES}")
if(lace_is_top_level)
    message(STATUS "  Build tests       : ${LACE_BUILD_TESTS}")
    message(STATUS "  Build benchmarks  : ${LACE_BUILD_BENCHMARKS}")
    message(STATUS "  Sanitize address  : ${LACE_SANITIZE_ADDRESS}")
    message(STATUS "  Sanitize thread   : ${LACE_SANITIZE_THREAD}")
    message(STATUS "  Sanitize UB       : ${LACE_SANITIZE_UB}")
endif()
message(STATUS "")
