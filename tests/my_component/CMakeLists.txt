add_executable(my_component_test my_component_test.cpp)

target_link_libraries(my_component_test PRIVATE project_options)
target_link_libraries(my_component_test PRIVATE project_features)
target_link_libraries(my_component_test PRIVATE GTest::gtest_main)
target_link_libraries(my_component_test PRIVATE my_component)

target_include_directories(my_component_test PRIVATE ${PROJECT_SOURCE_DIR}/src)

set_target_properties(my_component_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

gtest_discover_tests(my_component_test)
