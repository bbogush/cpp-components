include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
  URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(googlebenchmark)

set(CPP_COMPONENTS_BENCHMARK_EXECUTABLES "")

add_subdirectory(secure_websocket_client)

set(_benchmark_run_commands)
foreach(exe IN LISTS CPP_COMPONENTS_BENCHMARK_EXECUTABLES)
  list(APPEND _benchmark_run_commands COMMAND "$<TARGET_FILE:${exe}>")
endforeach()

add_custom_target(benchmarks-report
  ${_benchmark_run_commands}
  DEPENDS ${CPP_COMPONENTS_BENCHMARK_EXECUTABLES}
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  USES_TERMINAL
  COMMENT "Running all Google Benchmark targets"
  VERBATIM
)
