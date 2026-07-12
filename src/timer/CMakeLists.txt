find_package(Boost REQUIRED COMPONENTS system)

add_library(timer timer.cpp)

target_include_directories(timer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(timer PRIVATE project_options)
target_link_libraries(timer PRIVATE project_features)
target_link_libraries(timer PUBLIC executor Boost::headers)
target_link_libraries(timer PRIVATE Boost::system)
