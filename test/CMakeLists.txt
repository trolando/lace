# Discover all test_*.c files in this directory.
file(GLOB test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.c"
)
list(SORT test_sources)

function(add_lace_test_from_source src)
    get_filename_component(name "${src}" NAME_WE)

    add_executable(${name} "${src}")
    target_link_libraries(${name} PRIVATE lace::lace)

    # Better symbol names in backtraces on Linux.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${name} PRIVATE -rdynamic)
    endif()

    # Keep frame pointers in Debug / RelWithDebInfo for better stack traces.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
            $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
        )
    endif()

    # FreeBSD may need libexecinfo for backtrace support.
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        target_link_libraries(${name} PRIVATE execinfo)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

foreach(test_src IN LISTS test_sources)
    add_lace_test_from_source("${test_src}")
endforeach()