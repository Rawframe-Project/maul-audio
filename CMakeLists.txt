cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-audio)
set(MAUL_API_PREFIX maud)
set(MAUL_MACRO_PREFIX MAUD)
set(MAUL_TITLE "Maul Audio")
set(MAUL_PREFIX MAUL_AUDIO)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-audio/base.h MAUD MAUL_VERSION)
project(maul-audio
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Audio device I/O and spatial audio"
    LANGUAGES C)

option(MAUL_AUDIO_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_AUDIO_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_AUDIO_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_AUDIO_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_AUDIO_COVERAGE "Instrument for gcov coverage" OFF)

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_AUDIO_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-audio ${MAUL_LIBRARY_KIND}
    src/base.c
    src/buffer.c
    src/layout.c)
maul_configure_library(maul-audio)

if(MAUL_AUDIO_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(buffer)
    maul_add_test(layout)
    maul_add_header_tests()
endif()

if(MAUL_AUDIO_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_AUDIO_INSTALL)
    include(cmake/Install.cmake)
endif()
