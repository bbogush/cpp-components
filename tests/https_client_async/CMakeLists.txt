find_package(OpenSSL REQUIRED)

add_executable(https_client_async_test https_client_async_test.cpp)

target_link_libraries(https_client_async_test PRIVATE project_options)
target_link_libraries(https_client_async_test PRIVATE project_features)
target_link_libraries(https_client_async_test PRIVATE GTest::gtest_main)
target_link_libraries(https_client_async_test PRIVATE https_client_async)
target_link_libraries(https_client_async_test PRIVATE executor)

target_include_directories(https_client_async_test PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_definitions(https_client_async_test PRIVATE
  TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")

target_link_libraries(https_client_async_test PRIVATE OpenSSL::SSL)

set_target_properties(https_client_async_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gtest_discover_tests(https_client_async_test)
