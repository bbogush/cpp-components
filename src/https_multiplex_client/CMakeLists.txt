find_package(Boost REQUIRED COMPONENTS system)
find_package(CURL REQUIRED)

add_library(https_multiplex_client
  https_multiplex_client.cpp)

target_include_directories(https_multiplex_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(https_multiplex_client PRIVATE project_options)
target_link_libraries(https_multiplex_client PRIVATE project_features)
target_link_libraries(https_multiplex_client PRIVATE executor)
target_link_libraries(https_multiplex_client PRIVATE Boost::headers Boost::system CURL::libcurl)
