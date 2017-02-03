cmake_minimum_required(VERSION 3.1)

project(lace VERSION 1.0)

include(CTest)

set(PROJECT_DESCRIPTION "Lace, a work-stealing framework for multi-core fork-join parallelism")
set(PROJECT_URL "https://github.com/trolando/lace")

message(STATUS "CMake build configuration for Lace ${PROJECT_VERSION}")
message(STATUS "OS: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")

enable_language(C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -fno-strict-aliasing ${CMAKE_C_FLAGS}")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

option(LACE_PIE_TIMES "Record Pie Times" OFF)
option(LACE_COUNT_TASKS "Count Tasks in Lace" OFF)
option(LACE_COUNT_STEALS "Count Steals and Leaps in Lace" OFF)
option(LACE_COUNT_SPLITS "Count Splits in Lace" OFF)

option(BUILD_SHARED_LIBS "Enable/disable creation of shared libraries" OFF)
option(BUILD_STATIC_LIBS "Enable/disable creation of static libraries" ON)

option(LACE_BUILD_BENCHMARKS "Build benchmark programs" OFF)

find_package(Hwloc REQUIRED)

add_library(lace lace.c lace.h)
add_library(lace14 lace14.c lace14.h)
target_link_libraries(lace rt pthread ${HWLOC_LIBRARIES})
target_link_libraries(lace14 rt pthread ${HWLOC_LIBRARIES})
configure_file(lace_config.h.in lace_config.h)
add_custom_command(OUTPUT lace.h lace14.h lace14.c COMMAND ./gen.sh "${CMAKE_CURRENT_BINARY_DIR}" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS lace.sh lace.c)

if(LACE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

include(GNUInstallDirs)
install(TARGETS lace lace14 DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/lace.h ${CMAKE_CURRENT_BINARY_DIR}/lace14.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
# pkg-config
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lace.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/lace.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lace.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

add_subdirectory(test)
