find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

add_library(https_client_async
  https_client_async.cpp)

target_include_directories(https_client_async PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(https_client_async PRIVATE project_options)
target_link_libraries(https_client_async PRIVATE project_features)
target_link_libraries(https_client_async PRIVATE executor)
target_link_libraries(https_client_async PRIVATE Boost::headers Boost::system OpenSSL::SSL)
