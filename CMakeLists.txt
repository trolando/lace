cmake_minimum_required(VERSION 3.13)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

# Check if we are solo or as part of a larger project
# If we're a subproject, disable testing/benchmark tools
if(DEFINED PROJECT_NAME)
  set(subproject ON)
else()
  set(subproject OFF)
endif()

project(lace
    VERSION 1.2
    DESCRIPTION "Lace, a work-stealing framework for multi-core fork-join parallelism"
    HOMEPAGE_URL "https://github.com/trolando/lace"
    LANGUAGES C
)

include(CheckSymbolExists)
include(CMakeDependentOption)

message(STATUS "CMake build configuration for Lace ${PROJECT_VERSION}")
message(STATUS "OS: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo
            CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
            FORCE)
endif(NOT CMAKE_BUILD_TYPE)

option(LACE_PIE_TIMES "Let Lace record Pie times" OFF)
option(LACE_COUNT_TASKS "Let Lace record the number of tasks" OFF)
option(LACE_COUNT_STEALS "Let Lace count #steals and #leaps" OFF)
option(LACE_COUNT_SPLITS "Let Lace count #splits" OFF)
option(LACE_USE_HWLOC "Let Lace pin threads/memory using libhwloc" OFF)
option(LACE_USE_MMAP "Let Lace use mmap to allocate memory" ON)
cmake_dependent_option(LACE_TEST "Build and run Lace tests" ON "NOT subproject" OFF)
cmake_dependent_option(LACE_BUILD_BENCHMARKS "Build Lace benchmark programs" OFF "NOT subproject" OFF)

check_symbol_exists(mmap "sys/mman.h" HAVE_MMAP)
if(LACE_USE_MMAP AND NOT HAVE_MMAP)
    message(WARNING "mmap not found: disabling mmap support")
    set(LACE_USE_MMAP OFF)
endif()

add_library(lace STATIC)
target_sources(lace PRIVATE src/lace.c ${CMAKE_CURRENT_BINARY_DIR}/include/lace.h)
set_target_properties(lace PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/include/lace.h")
target_include_directories(lace 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_options(lace PRIVATE -Wall -Wextra -Werror -fno-strict-aliasing)
target_compile_features(lace PRIVATE c_std_11)
target_link_libraries(lace PUBLIC pthread)

add_library(lace14 STATIC)
target_sources(lace14 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/lace14.c ${CMAKE_CURRENT_BINARY_DIR}/include/lace14.h)
set_target_properties(lace14 PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/include/lace14.h")
target_include_directories(lace14
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_options(lace14 PRIVATE -Wall -Wextra -Werror -fno-strict-aliasing)
target_compile_features(lace14 PRIVATE c_std_11)
target_link_libraries(lace14 PUBLIC pthread)

if(LACE_USE_HWLOC)
    find_package(Hwloc REQUIRED)
    target_link_libraries(lace PUBLIC ${HWLOC_LIBRARIES})
    target_link_libraries(lace14 PUBLIC ${HWLOC_LIBRARIES})
endif()

add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/include/lace.h
        ${CMAKE_CURRENT_BINARY_DIR}/include/lace14.h
        ${CMAKE_CURRENT_BINARY_DIR}/lace14.c
    COMMAND src/gen.sh "${CMAKE_CURRENT_BINARY_DIR}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS src/lace.sh src/lace.c lace_config.h
)

configure_file(src/lace_config.h.in lace_config.h)

if(LACE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(LACE_TEST)
    # Add test directory and tests
    include(CTest)
    add_subdirectory(test)
endif()

# Setup installing and pkg-config
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/lace.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/lace.pc" @ONLY)

include(GNUInstallDirs)
install(TARGETS lace lace14
    EXPORT lace-export
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    INCLUDES DESTINATION include
)
install(EXPORT lace-export
    FILE
        LaceTargets.cmake
    NAMESPACE
        lace::
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/lace
)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/lace.pc
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

if(NOT subproject)
    # Setup basic CPack support
    set(CPACK_PACKAGE_VENDOR "tvandijk")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
    set(CMAKE_PROJECT_HOMEPAGE_URL ${PROJECT_URL})
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    include(CPack)
endif()
