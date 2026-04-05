cmake_minimum_required(VERSION 3.14)
project(lace-consumer-test LANGUAGES C)

find_package(lace REQUIRED)

add_executable(test-lace main.c)
target_link_libraries(test-lace PRIVATE lace::lace)