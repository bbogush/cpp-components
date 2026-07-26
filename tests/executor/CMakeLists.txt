add_executable(executor_test executor_test.cpp)

target_link_libraries(executor_test PRIVATE project_options)
target_link_libraries(executor_test PRIVATE project_features)
target_link_libraries(executor_test PRIVATE GTest::gtest_main)
target_link_libraries(executor_test PRIVATE cpp-components)

set_target_properties(executor_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gtest_discover_tests(executor_test)
