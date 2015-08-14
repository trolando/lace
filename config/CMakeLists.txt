# Import the CHECK_INCLUDE_FILES macros
include(CheckIncludeFiles)

# Test for available headers, etc
CHECK_INCLUDE_FILES("sys/time.h" LACE_CONFIG_HAVE_SYSTIME_H)
if (LACE_CONFIG_HAVE_SYSTIME_H)
	set(LACE_CONFIG_HAVE_SYSTIME_H_DEF "define")
else()
	set(LACE_CONFIG_HAVE_SYSTIME_H_DEF "undef")
endif()

CHECK_INCLUDE_FILES("sys/mman.h" LACE_CONFIG_HAVE_SYSMMAN_H)
if (LACE_CONFIG_HAVE_SYSMMAN_H)
	set(LACE_CONFIG_HAVE_SYSMMAN_H_DEF "define")
else()
	set(LACE_CONFIG_HAVE_SYSMMAN_H_DEF "undef")
endif()

CHECK_INCLUDE_FILES("unistd.h" LACE_CONFIG_HAVE_UNISTD_H)
if (LACE_CONFIG_HAVE_UNISTD_H)
	set(LACE_CONFIG_HAVE_UNISTD_H_DEF "define")
else()
	set(LACE_CONFIG_HAVE_UNISTD_H_DEF "undef")
endif()

CHECK_INCLUDE_FILES("sched.h" LACE_CONFIG_HAVE_SCHED_H)
if (LACE_CONFIG_HAVE_SCHED_H)
	set(LACE_CONFIG_HAVE_SCHED_H_DEF "define")
else()
	set(LACE_CONFIG_HAVE_SCHED_H_DEF "undef")
endif()

CHECK_INCLUDE_FILES("getopt.h" LACE_CONFIG_HAVE_GETOPT_H)
if (LACE_CONFIG_HAVE_GETOPT_H)
	set(LACE_CONFIG_HAVE_GETOPT_H_DEF "define")
else()
	set(LACE_CONFIG_HAVE_GETOPT_H_DEF "undef")
endif()

CHECK_INCLUDE_FILES("alloca.h" LACE_CONFIG_HAVE_ALLOCA_H)
if (LACE_CONFIG_HAVE_ALLOCA_H)
	set(LACE_CONFIG_HAVE_ALLOCA_H_DEF "define")
else()
	set(LACE_CONFIG_HAVE_ALLOCA_H_DEF "undef")
endif()

# Test for struct timespec
try_compile(LACE_CONFIG_HAVE_TIMESPEC ${PROJECT_BINARY_DIR} "${PROJECT_SOURCE_DIR}/config/cmake_check_struct_timespec_exists.c" OUTPUT_VARIABLE LACE_CONFIG_HAVE_TIMESPEC_OUPUT)
if (LACE_CONFIG_HAVE_TIMESPEC)
	set(LACE_CONFIG_HAVE_TIMESPEC_DEF "define")
else()
	set(LACE_CONFIG_HAVE_TIMESPEC_DEF "undef")
endif()

# Test whether a function declaration may contain "static inline"
try_compile(LACE_CONFIG_ISVALID_STATIC_INLINE ${PROJECT_BINARY_DIR} "${PROJECT_SOURCE_DIR}/config/cmake_static_inline_valid.c" OUTPUT_VARIABLE LACE_CONFIG_ISVALID_STATIC_INLINE_OUTPUT)
if (LACE_CONFIG_ISVALID_STATIC_INLINE)
	set(LACE_CONFIG_ISVALID_STATIC_INLINE_DEF "define")
else()
	set(LACE_CONFIG_ISVALID_STATIC_INLINE_DEF "undef")
endif()

# Configure a header file to pass some of the CMake settings to the source code
configure_file (
	"${PROJECT_SOURCE_DIR}/config/lace_config.h.in"
	"${PROJECT_BINARY_DIR}/lace_config.h"
)