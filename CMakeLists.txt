cmake_minimum_required(VERSION 3.25)
project(
  cpp-components
  VERSION 0.1.0
  LANGUAGES CXX)

option(BUILD_TESTING "Build unit tests" OFF)
option(BUILD_BENCHMARKS "Build Google Benchmark targets" OFF)
option(ENABLE_COVERAGE "Instrument cpp_components/ targets for code coverage" OFF)
option(ENABLE_CLANG_TIDY "Add clang-tidy static analysis target" OFF)
option(ENABLE_CPPCHECK "Add cppcheck static analysis target" OFF)
option(ENABLE_TSAN "Relax -Werror for ThreadSanitizer-incompatible atomics" OFF)
option(ENABLE_VALGRIND "Run unit tests under Valgrind Memcheck" OFF)

if(ENABLE_CLANG_TIDY OR ENABLE_CPPCHECK)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(ENABLE_TSAN)
  # "atomic_thread_fence" is not supported with "-fsanitize=thread"
  add_compile_options(-Wno-error=tsan)
endif()

if(ENABLE_VALGRIND)
  # gtest_discover_tests prefixes CMAKE_CROSSCOMPILING_EMULATOR onto each test command, including
  # discovery. Valgrind writes diagnostics to stderr, so gtest's stdout list stays parseable.
  find_program(VALGRIND_EXECUTABLE valgrind REQUIRED)
  set(CMAKE_CROSSCOMPILING_EMULATOR
      "${VALGRIND_EXECUTABLE}"
      "--tool=memcheck"
      "--leak-check=full"
      "--show-leak-kinds=definite,indirect"
      "--errors-for-leak-kinds=definite,indirect"
      "--track-origins=yes"
      "--error-exitcode=1")
endif()

add_library(project_options INTERFACE)
target_compile_options(
  project_options
  INTERFACE -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wsuggest-override
            -Wformat=2
            -Winit-self
            -Wdouble-promotion
            -Wcast-align
            -Wmissing-declarations
            -Wredundant-decls)

if(ENABLE_COVERAGE)
  target_compile_options(project_options INTERFACE --coverage -fprofile-update=atomic)
  target_link_options(project_options INTERFACE --coverage)
endif()

# cxx_std_26 selects the dialect; disable GNU extensions so targets compile as ISO C++26.
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(project_features INTERFACE)
target_compile_features(project_features INTERFACE cxx_std_26)

add_subdirectory(cpp_components)

# If embedded into another cmake project skip the tests
if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  return()
endif()

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(ENABLE_COVERAGE AND BUILD_TESTING)
  add_custom_target(
    coverage-report
    COMMAND
      "${CMAKE_COMMAND}" -DCOVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DCOVERAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/coverage_report.cmake
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating cpp_components/ coverage report (lcov CLI + HTML)"
    USES_TERMINAL VERBATIM)
endif()

add_custom_target(
  format-check
  COMMAND "${CMAKE_COMMAND}" -DFORMAT_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
          ${CMAKE_SOURCE_DIR}/cmake/format.cmake
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Checking clang-format, cmake-format, prettier and shfmt on tracked sources"
  USES_TERMINAL VERBATIM)

if(ENABLE_CLANG_TIDY)
  set(CLANG_TIDY_JOBS
      0
      CACHE STRING "Parallel clang-tidy jobs (0 = auto-detect CPU count)")

  add_custom_target(
    clang-tidy
    COMMAND
      "${CMAKE_COMMAND}" -DCLANG_TIDY_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DCLANG_TIDY_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DCLANG_TIDY_JOBS=${CLANG_TIDY_JOBS} -P
      ${CMAKE_SOURCE_DIR}/cmake/clang_tidy.cmake
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running clang-tidy on cpp_components/"
    USES_TERMINAL VERBATIM)
endif()

if(ENABLE_CPPCHECK)
  set(CPPCHECK_JOBS
      0
      CACHE STRING "Parallel cppcheck jobs (0 = auto-detect CPU count)")

  add_custom_target(
    cppcheck
    COMMAND
      "${CMAKE_COMMAND}" -DCPPCHECK_BINARY_DIR=${CMAKE_BINARY_DIR}
      -DCPPCHECK_SOURCE_DIR=${CMAKE_SOURCE_DIR} -DCPPCHECK_JOBS=${CPPCHECK_JOBS} -P
      ${CMAKE_SOURCE_DIR}/cmake/cppcheck.cmake
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running cppcheck"
    USES_TERMINAL VERBATIM)
endif()
