macro(set_compilation_settings NAME)
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    set_target_properties(${NAME} PROPERTIES C_STANDARD 11 CXX_STANDARD 11)
    target_compile_features(${NAME} PRIVATE c_std_11 cxx_std_11)

    # Warnings / basic opts
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${NAME} PRIVATE
            -pipe -Wall -Wextra -Wpedantic -Wno-deprecated
            $<$<BOOL:${LACE_NATIVE_OPT}>:-march=native>
        )
    endif()

    # LTO per toolchain, per-config
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${NAME} PRIVATE $<$<CONFIG:Release>:-flto>)
        target_link_options(${NAME} PRIVATE $<$<CONFIG:Release>:-flto>)
    elseif(MSVC)
        target_compile_options(${NAME} PRIVATE $<$<CONFIG:Release>:/GL>)
        target_link_options(${NAME} PRIVATE $<$<CONFIG:Release>:/LTCG>)
    endif()

    # libm only on platforms that have it
    if(UNIX)
        target_link_libraries(${NAME} PRIVATE m)
    endif()
endmacro(set_compilation_settings)

macro(add_lace_benchmark NAME)
    add_executable(${NAME} ${ARGN})
    if(MSVC)
        target_sources(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/getopt_win.c)
    endif()
    target_link_libraries(${NAME} PRIVATE lace::lace)
    set_compilation_settings(${NAME})
endmacro(add_lace_benchmark)

macro(add_lace14_benchmark NAME)
    add_executable(${NAME} ${ARGN})
    if(MSVC)
        target_sources(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/getopt_win.c)
    endif()
    target_link_libraries(${NAME} PRIVATE lace::lace14)
    set_compilation_settings(${NAME})
endmacro(add_lace14_benchmark)

macro(add_seq_benchmark NAME)
    add_executable(${NAME} ${ARGN})
    if(MSVC)
        target_sources(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/getopt_win.c)
    endif()
    set_compilation_settings(${NAME})
endmacro(add_seq_benchmark)

set(uts_sources uts/rng/brg_sha1.c uts/rng/brg_sha1.h uts/uts.c)

add_seq_benchmark(fib-seq fib/fib-seq.c)
add_seq_benchmark(fib-seq2 fib/fib-seq2.c)
add_seq_benchmark(cholesky-seq cholesky/cholesky.c)
add_seq_benchmark(fft-seq fft/fft.c)
add_seq_benchmark(integrate-seq integrate/integrate.c)
add_seq_benchmark(heat-seq heat/heat.c)
add_seq_benchmark(lu-seq lu/lu.c)
add_seq_benchmark(knapsack-seq knapsack/knapsack.c)
add_seq_benchmark(pi-seq pi/pi-seq.c)
add_seq_benchmark(pi-seq2 pi/pi-seq2.c)
add_seq_benchmark(quicksort-seq quicksort/quicksort.c)
add_seq_benchmark(rectmul-seq rectmul/rectmul.c)
add_seq_benchmark(uts-seq ${uts_sources} uts/uts-seq.c)
set_target_properties(uts-seq PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
add_seq_benchmark(nqueens-seq nqueens/nqueens-seq.c)
add_seq_benchmark(matmul-seq matmul/matmul-seq.c)
add_seq_benchmark(strassen-seq strassen/strassen-seq.c)
add_seq_benchmark(cilksort-seq cilksort/cilksort-seq.c)
add_seq_benchmark(dfs-seq dfs/dfs-seq.c)

add_lace_benchmark(fib-lace fib/fib-lace.c)
add_lace_benchmark(fib-cpp fib/fib-lace.cpp)
add_lace_benchmark(cholesky-lace cholesky/cholesky-lace.c)
add_lace_benchmark(fft-lace fft/fft-lace.c)
add_lace_benchmark(integrate-lace integrate/integrate-lace.c)
add_lace_benchmark(heat-lace heat/heat-lace.c)
add_lace_benchmark(lu-lace lu/lu-lace.c)
add_lace_benchmark(knapsack-lace knapsack/knapsack-lace.c)
add_lace_benchmark(pi-lace pi/pi-lace.c)
add_lace_benchmark(quicksort-lace quicksort/quicksort-lace.c)
add_lace14_benchmark(rectmul-lace rectmul/rectmul-lace.c)
add_lace_benchmark(uts2-lace ${uts_sources} uts/uts2-lace.c)
add_lace_benchmark(uts-lace ${uts_sources} uts/uts-lace.c)
set_target_properties(uts2-lace PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
set_target_properties(uts-lace PROPERTIES COMPILE_DEFINITIONS "BRG_C99_TYPES;BRG_RNG")
add_lace_benchmark(nqueens-lace nqueens/nqueens-lace.c)
add_lace_benchmark(matmul-lace matmul/matmul-lace.c)
add_lace_benchmark(strassen-lace strassen/strassen-lace.c)
add_lace_benchmark(cilksort-lace cilksort/cilksort-lace.c)
add_lace_benchmark(dfs-lace dfs/dfs-lace.c)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bench.py
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/bench.py ${CMAKE_CURRENT_BINARY_DIR}/bench.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bench.py
)

add_custom_target(copy_bench_py ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bench.py
)

# file(COPY bench.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
# file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/bench.py ${CMAKE_CURRENT_BINARY_DIR}/bench.py ONLY_IF_DIFFERENT)
