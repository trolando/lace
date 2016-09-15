cmake_minimum_required(VERSION 2.6)
project(lace C)

option(USE_HWLOC "Use HWLOC" ON)
option(PIE_TIMES "Record Pie Times" OFF)
option(COUNT_TASKS "Count Tasks in Lace" OFF)
option(COUNT_STEALS "Count Steals and Leaps in Lace" OFF)
option(COUNT_SPLITS "Count Splits in Lace" OFF)

include(CheckIncludeFiles)

if(USE_HWLOC)
  check_include_files(hwloc.h HAVE_HWLOC_H)
  if(HAVE_HWLOC_H)
    set (HWLOC_LIB hwloc)
    set (HWLOC_DEF "USE_HWLOC=1")
  else()
    message("No HWLOC library found!")
    set (HWLOC_DEF "USE_HWLOC=0")
  endif()
else()
  set (HWLOC_DEF "USE_HWLOC=0")
endif()

if(PIE_TIMES)
  set (LACE_PIE "LACE_PIE_TIMES=1")
else()
  set (LACE_PIE "LACE_PIE_TIMES=0")
endif()

if (COUNT_TASKS)
  set (LACE_COUNT_TASKS "LACE_COUNT_TASKS=1")
else()
  set (LACE_COUNT_TASKS "LACE_COUNT_TASKS=0")
endif()

if (COUNT_STEALS)
  set (LACE_COUNT_STEALS "LACE_COUNT_STEALS=1")
else()
  set (LACE_COUNT_STEALS "LACE_COUNT_STEALS=0")
endif()

if (COUNT_SPLITS)
  set (LACE_COUNT_SPLITS "LACE_COUNT_SPLITS=1")
else()
  set (LACE_COUNT_SPLITS "LACE_COUNT_SPLITS=0")
endif()

add_custom_command(OUTPUT lace.h lace-7.h lace-8.h lace-7.c lace-8.c COMMAND ./prepare.sh "${CMAKE_CURRENT_BINARY_DIR}" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS lace.sh lace.c)

set(CMAKE_C_FLAGS "-g -O3 -Wall -Wextra -Werror -I${CMAKE_CURRENT_SOURCE_DIR}")

set(lace_flags "${HWLOC_DEF};${LACE_PIE};${LACE_COUNT_TASKS};${LACE_COUNT_STEALS};${LACE_COUNT_SPLITS}")

set(uts_sources benchmarks/uts/rng/brg_sha1.c benchmarks/uts/rng/brg_sha1.h benchmarks/uts/uts.c)


add_executable(fib-seq benchmarks/fib/fib-seq.c)
target_link_libraries(fib-seq m)

add_executable(fib-seq2 benchmarks/fib/fib-seq2.c)
target_link_libraries(fib-seq2 m)

add_executable(fib-lace benchmarks/fib/fib-lace.c lace.c lace.h)
set_target_properties(fib-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(fib-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(fib-lace pthread m ${HWLOC_LIB})

# add_executable(fib-lace-prof benchmarks/fib/fib-lace.c lace.h)
# set_target_properties(fib-lace-prof PROPERTIES COMPILE_FLAGS "-I. -g -fno-inline")
# set_target_properties(fib-lace-prof PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
# target_link_libraries(fib-lace-prof pthread m profiler ${HWLOC_LIB})

add_executable(pi-lace benchmarks/pi/pi-lace.c lace.c lace.h)
set_target_properties(pi-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(pi-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(pi-lace pthread m ${HWLOC_LIB})

add_executable(pi-seq benchmarks/pi/pi-seq.c)
target_link_libraries(pi-seq m)

add_executable(pi-seq2 benchmarks/pi/pi-seq2.c)
target_link_libraries(pi-seq2 m)

add_executable(uts-seq ${uts_sources} benchmarks/uts/uts-seq.c)
set_target_properties(uts-seq PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
target_link_libraries(uts-seq m)

add_executable(uts2-lace ${uts_sources} benchmarks/uts/uts2-lace.c lace.c lace.h)
set_target_properties(uts2-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(uts2-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags};BRG_C99_TYPES;BRG_RNG")
target_link_libraries(uts2-lace m pthread ${HWLOC_LIB})

add_executable(uts-lace ${uts_sources} benchmarks/uts/uts-lace.c lace.c lace.h)
set_target_properties(uts-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(uts-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags};BRG_C99_TYPES;BRG_RNG")
target_link_libraries(uts-lace m pthread ${HWLOC_LIB})

# add_executable(uts-lace-prof ${uts_sources} benchmarks/uts/uts-lace.c lace.c lace.h)
# set_target_properties(uts-lace-prof PROPERTIES COMPILE_FLAGS "-I. -g -fno-inline")
# set_target_properties(uts-lace-prof PROPERTIES COMPILE_DEFINITIONS "${lace_flags};BRG_C99_TYPES;BRG_RNG")
# target_link_libraries(uts-lace-prof m pthread profiler ${HWLOC_LIB})

add_executable(queens-lace benchmarks/queens/queens-lace.c lace.c lace.h)
set_target_properties(queens-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(queens-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(queens-lace pthread m ${HWLOC_LIB})

# add_executable(queens-lace-prof benchmarks/queens/queens-lace.c lace.c lace.h)
# set_target_properties(queens-lace-prof PROPERTIES COMPILE_FLAGS "-I. -g -fno-inline")
# set_target_properties(queens-lace-prof PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
# target_link_libraries(queens-lace-prof pthread m profiler ${HWLOC_LIB})

add_executable(queens-seq benchmarks/queens/queens-seq.c)
target_link_libraries(queens-seq m)

add_executable(matmul-lace benchmarks/matmul/matmul-lace.c lace-8.c lace-8.h)
set_target_properties(matmul-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(matmul-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(matmul-lace pthread m ${HWLOC_LIB})

add_executable(matmul-seq benchmarks/matmul/matmul-seq.c)
target_link_libraries(matmul-seq m)

#add_executable(matmul-lace-prof benchmarks/matmul/matmul-lace.c lace-8.c lace-8.h)
#set_target_properties(matmul-lace-prof PROPERTIES COMPILE_FLAGS "-I. -g -fno-inline")
#set_target_properties(matmul-lace-prof PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
#target_link_libraries(matmul-lace-prof pthread m profiler ${HWLOC_LIB})

add_executable(knapsack-lace benchmarks/knapsack/knapsack-lace.c lace.c lace.h)
set_target_properties(knapsack-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(knapsack-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(knapsack-lace pthread m ${HWLOC_LIB})

add_executable(strassen-lace benchmarks/strassen/strassen-lace.c lace-7.c lace-7.h)
set_target_properties(strassen-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(strassen-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(strassen-lace pthread m ${HWLOC_LIB})

#add_executable(strassen-lace-prof benchmarks/strassen/strassen-lace.c lace-7.c lace-7.h)
#set_target_properties(strassen-lace-prof PROPERTIES COMPILE_FLAGS "-I. -g -fno-inline")
#set_target_properties(strassen-lace-prof PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
#target_link_libraries(strassen-lace-prof pthread m profiler  ${HWLOC_LIB})

add_executable(cilksort-lace benchmarks/cilksort/cilksort-lace.c lace.c lace.h)
set_target_properties(cilksort-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(cilksort-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(cilksort-lace pthread m ${HWLOC_LIB})

add_executable(cilksort-seq benchmarks/cilksort/cilksort-seq.c)
target_link_libraries(cilksort-seq m)

#add_executable(cilksort-lace-prof benchmarks/cilksort/cilksort-lace.c lace.c lace.h)
#set_target_properties(cilksort-lace-prof PROPERTIES COMPILE_FLAGS "-I. -g -fno-inline")
#set_target_properties(cilksort-lace-prof PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
#target_link_libraries(cilksort-lace-prof pthread m profiler ${HWLOC_LIB})

add_executable(dfs-lace benchmarks/dfs/dfs-lace.c lace.c lace.h)
set_target_properties(dfs-lace PROPERTIES COMPILE_FLAGS "-I.")
set_target_properties(dfs-lace PROPERTIES COMPILE_DEFINITIONS "${lace_flags}")
target_link_libraries(dfs-lace pthread m ${HWLOC_LIB})

add_executable(dfs-seq benchmarks/dfs/dfs-seq.c)
target_link_libraries(dfs-seq m)
