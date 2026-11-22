# Top-level build for the Alamo rewrite: Phase 1 tools + Phase 2 engine (engine/).
cmake_minimum_required(VERSION 3.20)
project(alamo_rewrite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  message(FATAL_ERROR "Use Microsoft cl.exe for Windows x64. Run dev.cmd configure.")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /EHsc)

option(ALAMO_RELEASE_PDB "MSVC Release: write PDBs (/Z7 + /DEBUG) so crash dumps show function names" OFF)
if(MSVC AND (ALAMO_RELEASE_PDB OR "$ENV{ALAMO_RELEASE_PDB}" STREQUAL "1"))
  message(STATUS "Release PDBs: on")
  add_compile_options($<$<CONFIG:Release>:/Z7>)
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF> $<$<CONFIG:Release>:/INCREMENTAL:NO>)
endif()

# MSVC AddressSanitizer, the nightly "asan" preset (dev.cmd check --preset asan; docs/DECISIONS.md "MSVC is the only
# compiler"). Every C and C++ target is instrumented: MSVC's std::string and std::vector annotations must agree between
# the code that writes a container and the code that reads it, so mixing instrumented and plain code reports false
# use-after-poison. /Z7 gives ASan the debug info for its reports (without it, warning C5072 is an error under /WX).
# The linker adds the ASan runtime (/INFERASANLIBS, the default); the tests find its DLL on the MSVC environment's PATH.
# Release already avoids the incompatible /RTC, /ZI and incremental linking. The asan preset also sets
# ALAMO_RELEASE_PDB for the linker's /DEBUG, so the reports name functions and lines.
option(ALAMO_ASAN "Compile everything with MSVC /fsanitize=address" OFF)
if(ALAMO_ASAN)
  message(STATUS "AddressSanitizer: on")
  add_compile_options(/fsanitize=address /Z7)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

enable_testing()
# Suite labels (synthetic / retail / vulkan / traces), the prerequisite gate and companion entries for mixed tests.
# Included before any test is added: it overrides add_test.
include(cmake/AlamoTestSuites.cmake)
add_subdirectory(tools/meg)
add_subdirectory(tools/formats)
add_subdirectory(tools/shaders/cpp)

add_subdirectory(engine)
add_subdirectory(tools/parity)
add_subdirectory(tools/fuzz)

# A tests/test_suites.txt rule that matches no configured test is stale (renamed test, or module not configured).
# Deferred: engine/render and engine/apps/game add some tests in deferred calls of this directory.
cmake_language(DEFER CALL alamo_test_suites_check)
