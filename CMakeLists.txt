cmake_minimum_required(VERSION 3.20)
project(cpp-components VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_TESTING "Build unit tests" OFF)
option(ENABLE_COVERAGE "Instrument src/ targets for code coverage" OFF)
option(ENABLE_CLANG_TIDY "Add clang-tidy static analysis target" OFF)

if(ENABLE_CLANG_TIDY)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

add_library(project_options INTERFACE)
target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic -Werror)

if(ENABLE_COVERAGE)
  target_compile_options(project_options INTERFACE --coverage -fprofile-update=atomic)
  target_link_options(project_options INTERFACE --coverage)
endif()

add_library(project_features INTERFACE)
target_compile_features(project_features INTERFACE cxx_std_20)

add_subdirectory(src)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

if(ENABLE_COVERAGE AND BUILD_TESTING)
  add_custom_target(coverage-report
    COMMAND "${CMAKE_COMMAND}"
            -DCOVERAGE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/coverage_report.cmake
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating src/ coverage report (lcov CLI + HTML)"
    USES_TERMINAL
    VERBATIM
  )
endif()

if(ENABLE_CLANG_TIDY)
  set(CLANG_TIDY_JOBS 0 CACHE STRING "Parallel clang-tidy jobs (0 = auto-detect CPU count)")

  add_custom_target(clang-tidy
    COMMAND "${CMAKE_COMMAND}"
            -DCLANG_TIDY_BINARY_DIR=${CMAKE_BINARY_DIR}
            -DCLANG_TIDY_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCLANG_TIDY_JOBS=${CLANG_TIDY_JOBS}
            -P ${CMAKE_SOURCE_DIR}/cmake/clang_tidy.cmake
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running clang-tidy on src/"
    USES_TERMINAL
    VERBATIM
  )
endif()
