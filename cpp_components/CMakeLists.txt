find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

add_library(cpp_components STATIC
  executor/executor.cpp
  timer/timer.cpp
  secure_websocket_client/secure_websocket_client.cpp
  secure_websocket_client/reconnecting_secure_websocket_client.cpp
  https_client_async/https_client_async.cpp
  https_multiplex_client/https_multiplex_client.cpp
)

add_library(cpp_components::cpp_components ALIAS cpp_components)

target_include_directories(cpp_components PUBLIC
  ${PROJECT_SOURCE_DIR}
)

target_link_libraries(cpp_components PRIVATE project_options)
target_link_libraries(cpp_components PUBLIC project_features)
target_link_libraries(cpp_components PUBLIC
  Boost::headers Boost::system OpenSSL::SSL CURL::libcurl
)
