cmake_minimum_required(VERSION 3.1)

project(lace VERSION 1.1 LANGUAGES C)

include(CTest)

set(PROJECT_DESCRIPTION "Lace, a work-stealing framework for multi-core fork-join parallelism")
set(PROJECT_URL "https://github.com/trolando/lace")

message(STATUS "CMake build configuration for Lace ${PROJECT_VERSION}")
message(STATUS "OS: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

option(LACE_PIE_TIMES "Record Pie Times" OFF)
option(LACE_COUNT_TASKS "Count Tasks in Lace" OFF)
option(LACE_COUNT_STEALS "Count Steals and Leaps in Lace" OFF)
option(LACE_COUNT_SPLITS "Count Splits in Lace" OFF)
option(LACE_USE_HWLOC "Enable/disable using libhwloc to pin threads and memory" OFF)
option(LACE_USE_MMAP "Enable/disable using mmap to allocate memory" OFF)


option(LACE_BUILD_BENCHMARKS "Build benchmark programs" OFF)

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

set_target_properties(lace lace14 PROPERTIES C_STANDARD 11)

configure_file(lace_config.h.in lace_config.h)
add_custom_command(OUTPUT lace.h lace14.h lace14.c COMMAND ./gen.sh "${CMAKE_CURRENT_BINARY_DIR}" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS lace.sh lace.c)

if(LACE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Add test directory and tests
add_subdirectory(test)

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
