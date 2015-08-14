cmake_minimum_required(VERSION 2.8)
project(getopt C)

include_directories("${PROJECT_SOURCE_DIR}")

if (MSVC)
	add_definitions("/DEXPORTS_GETOPT")
	add_library(getopt SHARED "getopt.c" "getopt.h")
endif()
