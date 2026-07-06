add_executable(executor_test executor_test.cpp)

target_link_libraries(executor_test PRIVATE project_options)
target_link_libraries(executor_test PRIVATE project_features)
target_link_libraries(executor_test PRIVATE GTest::gtest_main)
target_link_libraries(executor_test PRIVATE executor)

target_include_directories(executor_test PRIVATE ${PROJECT_SOURCE_DIR}/src)

set_target_properties(executor_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gtest_discover_tests(executor_test)
