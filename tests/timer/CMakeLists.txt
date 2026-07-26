add_executable(timer_test timer_test.cpp)

target_link_libraries(timer_test PRIVATE project_options)
target_link_libraries(timer_test PRIVATE project_features)
target_link_libraries(timer_test PRIVATE GTest::gtest_main)
target_link_libraries(timer_test PRIVATE cpp-components)

set_target_properties(timer_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gtest_discover_tests(timer_test)
