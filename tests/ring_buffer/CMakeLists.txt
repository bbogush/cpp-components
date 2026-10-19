add_executable(ring_buffer_test ring_buffer_test.cpp)

target_link_libraries(ring_buffer_test PRIVATE project_options)
target_link_libraries(ring_buffer_test PRIVATE project_features)
target_link_libraries(ring_buffer_test PRIVATE GTest::gtest_main)
target_link_libraries(ring_buffer_test PRIVATE cpp_components)

set_target_properties(ring_buffer_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gtest_discover_tests(ring_buffer_test PROPERTIES TIMEOUT 120)
