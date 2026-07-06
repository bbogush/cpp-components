find_package(Boost REQUIRED COMPONENTS system)

add_library(executor executor.cpp)

target_link_libraries(executor PRIVATE project_options)
target_link_libraries(executor PRIVATE project_features)
target_link_libraries(executor PRIVATE Boost::headers Boost::system)
