find_package(OpenSSL REQUIRED)

set(_secure_websocket_client_benchmark_executables
  secure_websocket_client_echo_benchmark
  secure_websocket_client_write_benchmark
  secure_websocket_client_read_benchmark
)

function(add_secure_websocket_client_benchmark target source)
  add_executable(${target} ${source} benchmark_support.cpp)

  target_link_libraries(${target} PRIVATE project_options)
  target_link_libraries(${target} PRIVATE project_features)
  target_link_libraries(${target} PRIVATE benchmark::benchmark)
  target_link_libraries(${target} PRIVATE benchmark::benchmark_main)
  target_link_libraries(${target} PRIVATE cpp_components)
  target_link_libraries(${target} PRIVATE OpenSSL::SSL)

  target_compile_definitions(${target} PRIVATE
    BENCHMARK_CERT_DIR="${CMAKE_SOURCE_DIR}/tests/secure_websocket_client/certs")

  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
endfunction()

add_secure_websocket_client_benchmark(
  secure_websocket_client_echo_benchmark echo_benchmark.cpp)
add_secure_websocket_client_benchmark(
  secure_websocket_client_write_benchmark write_benchmark.cpp)
add_secure_websocket_client_benchmark(
  secure_websocket_client_read_benchmark read_benchmark.cpp)

set(CPP_COMPONENTS_BENCHMARK_EXECUTABLES
  ${CPP_COMPONENTS_BENCHMARK_EXECUTABLES}
  ${_secure_websocket_client_benchmark_executables}
  PARENT_SCOPE)
