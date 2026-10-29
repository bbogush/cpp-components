add_executable(fixed_ring_buffer_test fixed_ring_buffer_test.cpp)

target_link_libraries(fixed_ring_buffer_test PRIVATE project_options)
target_link_libraries(fixed_ring_buffer_test PRIVATE project_features)
target_link_libraries(fixed_ring_buffer_test PRIVATE GTest::gtest_main)
target_link_libraries(fixed_ring_buffer_test PRIVATE cpp_components)

set_target_properties(fixed_ring_buffer_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                        "${CMAKE_BINARY_DIR}")

gtest_discover_tests(
  fixed_ring_buffer_test
  PROPERTIES
  TIMEOUT 120)
