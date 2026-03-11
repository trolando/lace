# cmake/LaceDocs.cmake
#
# Adds a 'docs' target that:
#   1. Runs Doxygen on src/lace.h → XML
#   2. Runs Sphinx + Breathe   → HTML
#
# Usage in CMakeLists.txt:
#   if(lace_is_top_level AND LACE_BUILD_DOCS)
#       include(LaceDocs)
#   endif()

find_package(Doxygen REQUIRED)
find_program(SPHINX_BUILD sphinx-build
    HINTS $ENV{HOME}/.local/bin /usr/local/bin
    DOC "Path to sphinx-build"
)
if(NOT SPHINX_BUILD)
    message(FATAL_ERROR "sphinx-build not found. Install: pip install sphinx breathe sphinx-rtd-theme")
endif()

# Where Doxygen writes its XML
set(DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doxygen")

# Configure Doxyfile
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY
)

# Configure Sphinx conf.py
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/conf.py.in"
    "${CMAKE_CURRENT_BINARY_DIR}/docs/conf.py"
    @ONLY
)

set(SPHINX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/docs")
set(SPHINX_BUILD_DIR  "${CMAKE_CURRENT_BINARY_DIR}/docs/_build")

# Step 1: Doxygen → XML
add_custom_target(doxygen
    COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating Doxygen XML for Lace..."
    VERBATIM
)

# Step 2: Sphinx + Breathe → HTML
# -c points Sphinx at the *build* dir where the configured conf.py lives
add_custom_target(docs
    COMMAND ${SPHINX_BUILD}
        -b html
        -c "${CMAKE_CURRENT_BINARY_DIR}/docs"
        "${SPHINX_SOURCE_DIR}"
        "${SPHINX_BUILD_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Building Lace HTML documentation..."
    VERBATIM
)
add_dependencies(docs doxygen)

message(STATUS "Documentation targets: 'doxygen', 'docs'")
message(STATUS "  cmake --build . --target docs")
message(STATUS "  Output: ${SPHINX_BUILD_DIR}/index.html")
