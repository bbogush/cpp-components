add_executable(logger_test logger_test.cpp)

target_link_libraries(logger_test PRIVATE project_options)
target_link_libraries(logger_test PRIVATE project_features)
target_link_libraries(logger_test PRIVATE GTest::gtest_main)
target_link_libraries(logger_test PRIVATE cpp_components)

set_target_properties(logger_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

gtest_discover_tests(
  logger_test
  PROPERTIES
  TIMEOUT 120)
