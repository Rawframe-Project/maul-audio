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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAUL_AUDIO_PIPEWIRE_DEFAULT ON)
else()
    set(MAUL_AUDIO_PIPEWIRE_DEFAULT OFF)
endif()
option(MAUL_AUDIO_PIPEWIRE "Build the PipeWire backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_PULSE "Build the PulseAudio backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})

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
    src/allocator.c
    src/backend_offline.c
    src/base.c
    src/buffer.c
    src/context.c
    src/device.c
    src/follow.c
    src/layout.c
    src/notify.c
    src/offline.c
    src/period.c
    src/stream.c
    src/thread.c)
maul_configure_library(maul-audio)

# PipeWire's headers at build time only: the library opens libpipewire
# at run time and links nothing of it.
if(MAUL_AUDIO_PIPEWIRE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAUL_AUDIO_PW REQUIRED libpipewire-0.3)
    target_sources(maul-audio PRIVATE src/backend_pipewire.c src/pipewire_api.c
        src/pipewire_stream.c)
    target_include_directories(maul-audio SYSTEM PRIVATE ${MAUL_AUDIO_PW_INCLUDE_DIRS})
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_PIPEWIRE=1)
    # PipeWire's headers use POSIX and GNU declarations (ssize_t, locale_t)
    # that a strict C23 build hides.
    set_source_files_properties(src/backend_pipewire.c src/pipewire_api.c src/pipewire_stream.c
        PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    # PipeWire's method macros compare a uint32_t version with an int
    # inside the code they expand to; the comparison is theirs.
    set_source_files_properties(src/backend_pipewire.c src/pipewire_stream.c
        PROPERTIES COMPILE_OPTIONS -Wno-sign-compare)
    target_link_libraries(maul-audio PRIVATE ${CMAKE_DL_LIBS})
endif()

# Likewise libpulse's headers only.
if(MAUL_AUDIO_PULSE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAUL_AUDIO_PA REQUIRED libpulse)
    target_sources(maul-audio PRIVATE src/backend_pulse.c src/pulse_api.c)
    target_include_directories(maul-audio SYSTEM PRIVATE ${MAUL_AUDIO_PA_INCLUDE_DIRS})
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_PULSE=1)
    set_source_files_properties(src/backend_pulse.c src/pulse_api.c
        PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    target_link_libraries(maul-audio PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MAUL_AUDIO_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(buffer)
    maul_add_test(layout)
    maul_add_test(context)
    maul_add_test(stream)
    maul_add_test(device)
    if(MAUL_AUDIO_PIPEWIRE)
        # The test's own client links libpipewire; the library does not.
        pkg_check_modules(MAUL_AUDIO_PW_LINK REQUIRED libpipewire-0.3)
        maul_add_test(pipewire THREADS)
        target_include_directories(test_pipewire SYSTEM PRIVATE ${MAUL_AUDIO_PW_INCLUDE_DIRS})
        target_compile_definitions(test_pipewire PRIVATE _GNU_SOURCE)
        target_compile_options(test_pipewire PRIVATE -Wno-sign-compare)
        target_link_libraries(test_pipewire PRIVATE ${MAUL_AUDIO_PW_LINK_LIBRARIES})
        maul_add_test(pipewire_restart THREADS)
        target_compile_definitions(test_pipewire_restart PRIVATE _GNU_SOURCE)
        # Both use the one test daemon, and the second one restarts it.
        set_tests_properties(pipewire pipewire_restart PROPERTIES SKIP_RETURN_CODE 77
            RESOURCE_LOCK pipewire-daemon)
    endif()
    if(MAUL_AUDIO_PULSE)
        maul_add_test(pulse)
        target_compile_definitions(test_pulse PRIVATE _GNU_SOURCE)
        # The test server is the PipeWire daemon's pulse service.
        set_tests_properties(pulse PROPERTIES SKIP_RETURN_CODE 77 RESOURCE_LOCK pipewire-daemon)
    endif()
    # The audio-thread trap exists only where invariants are checked.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(trap WHITEBOX POSIX)
    endif()
    maul_add_header_tests()
endif()

if(MAUL_AUDIO_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_AUDIO_INSTALL)
    include(cmake/Install.cmake)
endif()
