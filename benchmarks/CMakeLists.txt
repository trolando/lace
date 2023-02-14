include_directories(".")

add_executable(fib-seq fib/fib-seq.c)
set_property(TARGET fib-seq PROPERTY C_STANDARD 11)
target_link_libraries(fib-seq m pthread)

add_executable(fib-seq2 fib/fib-seq2.c)
set_property(TARGET fib-seq2 PROPERTY C_STANDARD 11)
target_link_libraries(fib-seq2 m pthread)

add_executable(fib-lace fib/fib-lace.c)
set_property(TARGET fib-lace PROPERTY C_STANDARD 11)
target_link_libraries(fib-lace m lace pthread)

add_executable(fib-lace-cpp fib/fib-lace.cpp)
set_property(TARGET fib-lace-cpp PROPERTY CXX_STANDARD 11)
target_link_libraries(fib-lace-cpp m lace pthread)

add_executable(pi-lace pi/pi-lace.c)
set_property(TARGET pi-lace PROPERTY C_STANDARD 11)
target_link_libraries(pi-lace m lace pthread)

add_executable(pi-seq pi/pi-seq.c)
set_property(TARGET pi-seq PROPERTY C_STANDARD 11)
target_link_libraries(pi-seq m pthread)

add_executable(pi-seq2 pi/pi-seq2.c)
set_property(TARGET pi-seq2 PROPERTY C_STANDARD 11)
target_link_libraries(pi-seq2 m pthread)

set(uts_sources uts/rng/brg_sha1.c uts/rng/brg_sha1.h uts/uts.c)

add_executable(uts-seq ${uts_sources} uts/uts-seq.c)
set_target_properties(uts-seq PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
set_property(TARGET uts-seq PROPERTY C_STANDARD 11)
target_link_libraries(uts-seq m pthread)

add_executable(uts2-lace ${uts_sources} uts/uts2-lace.c)
set_target_properties(uts2-lace PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
set_property(TARGET uts2-lace PROPERTY C_STANDARD 11)
target_link_libraries(uts2-lace m lace pthread)

add_executable(uts-lace ${uts_sources} uts/uts-lace.c)
set_target_properties(uts-lace PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
set_property(TARGET uts-lace PROPERTY C_STANDARD 11)
target_link_libraries(uts-lace m lace pthread)

add_executable(queens-lace queens/queens-lace.c)
set_property(TARGET queens-lace PROPERTY C_STANDARD 11)
target_link_libraries(queens-lace m lace pthread)

add_executable(queens-seq queens/queens-seq.c)
set_property(TARGET queens-seq PROPERTY C_STANDARD 11)
target_link_libraries(queens-seq m pthread)

add_executable(matmul-lace matmul/matmul-lace.c)
set_property(TARGET matmul-lace PROPERTY C_STANDARD 11)
target_link_libraries(matmul-lace m lace14 pthread)

add_executable(matmul-seq matmul/matmul-seq.c)
set_property(TARGET matmul-seq PROPERTY C_STANDARD 11)
target_link_libraries(matmul-seq m pthread)

add_executable(knapsack-lace knapsack/knapsack-lace.c)
set_property(TARGET knapsack-lace PROPERTY C_STANDARD 11)
target_link_libraries(knapsack-lace m lace pthread)

add_executable(strassen-lace strassen/strassen-lace.c)
set_property(TARGET strassen-lace PROPERTY C_STANDARD 11)
target_link_libraries(strassen-lace m lace14 pthread)

add_executable(cilksort-lace cilksort/cilksort-lace.c)
set_property(TARGET cilksort-lace PROPERTY C_STANDARD 11)
target_link_libraries(cilksort-lace m lace pthread)

add_executable(cilksort-seq cilksort/cilksort-seq.c)
set_property(TARGET cilksort-seq PROPERTY C_STANDARD 11)
target_link_libraries(cilksort-seq m pthread)

add_executable(dfs-lace dfs/dfs-lace.c)
set_property(TARGET dfs-lace PROPERTY C_STANDARD 11)
target_link_libraries(dfs-lace m lace pthread)

add_executable(dfs-seq dfs/dfs-seq.c)
set_property(TARGET dfs-seq PROPERTY C_STANDARD 11)
target_link_libraries(dfs-seq m pthread)

file(COPY bench.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
