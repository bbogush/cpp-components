add_subdirectory(executor)

add_executable(${PROJECT_NAME} main.cpp)

set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE project_options)
target_link_libraries(${PROJECT_NAME} PRIVATE project_features)
target_link_libraries(${PROJECT_NAME} PRIVATE executor)
