include_directories(".")

add_executable(fib-seq fib/fib-seq.c)
target_link_libraries(fib-seq m)

add_executable(fib-seq2 fib/fib-seq2.c)
target_link_libraries(fib-seq2 m)

add_executable(fib-lace fib/fib-lace.c)
target_link_libraries(fib-lace m lace)

add_executable(pi-lace pi/pi-lace.c)
target_link_libraries(pi-lace m lace)

add_executable(pi-seq pi/pi-seq.c)
target_link_libraries(pi-seq m)

add_executable(pi-seq2 pi/pi-seq2.c)
target_link_libraries(pi-seq2 m)

set(uts_sources uts/rng/brg_sha1.c uts/rng/brg_sha1.h uts/uts.c)

add_executable(uts-seq ${uts_sources} uts/uts-seq.c)
set_target_properties(uts-seq PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
target_link_libraries(uts-seq m)

add_executable(uts2-lace ${uts_sources} uts/uts2-lace.c)
set_target_properties(uts2-lace PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
target_link_libraries(uts2-lace m lace)

add_executable(uts-lace ${uts_sources} uts/uts-lace.c)
set_target_properties(uts-lace PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
target_link_libraries(uts-lace m lace)

add_executable(queens-lace queens/queens-lace.c)
target_link_libraries(queens-lace m lace)

add_executable(queens-seq queens/queens-seq.c)
target_link_libraries(queens-seq m)

add_executable(matmul-lace matmul/matmul-lace.c)
target_link_libraries(matmul-lace m lace14)

add_executable(matmul-seq matmul/matmul-seq.c)
target_link_libraries(matmul-seq m)

add_executable(knapsack-lace knapsack/knapsack-lace.c)
target_link_libraries(knapsack-lace m lace)

add_executable(strassen-lace strassen/strassen-lace.c)
target_link_libraries(strassen-lace m lace14)

add_executable(cilksort-lace cilksort/cilksort-lace.c)
target_link_libraries(cilksort-lace m lace)

add_executable(cilksort-seq cilksort/cilksort-seq.c)
target_link_libraries(cilksort-seq m)

add_executable(dfs-lace dfs/dfs-lace.c)
target_link_libraries(dfs-lace m lace)

add_executable(dfs-seq dfs/dfs-seq.c)
target_link_libraries(dfs-seq m)

file(COPY bench.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
