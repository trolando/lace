cmake_minimum_required(VERSION 3.8)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

# Check if we are solo or as part of a larger project
# If we're a subproject, this disabled testing/benchmark tools
if(DEFINED PROJECT_NAME)
  set(subproject ON)
else()
  set(subproject OFF)
endif()

project(lace VERSION 1.2 LANGUAGES C)

include(CheckSymbolExists)
include(CMakeDependentOption)

set(PROJECT_DESCRIPTION "Lace, a work-stealing framework for multi-core fork-join parallelism")
set(PROJECT_URL "https://github.com/trolando/lace")

message(STATUS "CMake build configuration for Lace ${PROJECT_VERSION}")
message(STATUS "OS: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

# Set the C standard; this is a directory scope variable, so does not propagate to parent scopes
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

if(LACE_USE_HWLOC)
    find_package(Hwloc REQUIRED)
endif()

add_library(lace STATIC lace.c lace.h)
target_include_directories(lace PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(lace PRIVATE -Wall -Wextra -Werror -fno-strict-aliasing)
target_link_libraries(lace rt pthread ${HWLOC_LIBRARIES})

add_library(lace14 STATIC lace14.c lace14.h)
target_include_directories(lace14 PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(lace14 rt pthread ${HWLOC_LIBRARIES})
target_compile_options(lace14 PRIVATE -Wall -Wextra -Werror -fno-strict-aliasing)

configure_file(lace_config.h.in lace_config.h)
add_custom_command(OUTPUT lace.h lace14.h lace14.c COMMAND ./gen.sh "${CMAKE_CURRENT_BINARY_DIR}" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS lace.sh lace.c)

if(LACE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(LACE_TEST)
    # Add test directory and tests
    include(CTest)
    add_subdirectory(test)
endif()

if(NOT subproject)
    # Setup installing and pkg-config
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lace.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/lace.pc" @ONLY)

    include(GNUInstallDirs)
    install(TARGETS lace lace14 DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/lace.h
        ${CMAKE_CURRENT_BINARY_DIR}/lace14.h
        ${CMAKE_CURRENT_BINARY_DIR}/lace_config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/lace.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    # Setup basic CPack support
    set(CPACK_PACKAGE_VENDOR "tvandijk")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
    set(CMAKE_PROJECT_HOMEPAGE ${PROJECT_URL})
    set(CMAKE_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
    set(CMAKE_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/Readme.md")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    include(CPack)
endif()
