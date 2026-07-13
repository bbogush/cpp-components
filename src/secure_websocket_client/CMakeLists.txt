find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

add_library(secure_websocket_client secure_websocket_client.cpp)

target_include_directories(secure_websocket_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(secure_websocket_client PRIVATE project_options)
target_link_libraries(secure_websocket_client PRIVATE project_features)
target_link_libraries(secure_websocket_client PRIVATE executor)
target_link_libraries(secure_websocket_client PRIVATE timer)
target_link_libraries(secure_websocket_client PRIVATE Boost::headers Boost::system OpenSSL::SSL)
