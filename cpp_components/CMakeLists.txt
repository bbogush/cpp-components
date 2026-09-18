find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

find_library(URING_LIBRARY NAMES uring)
if(NOT URING_LIBRARY)
  message(FATAL_ERROR "liburing not found; install liburing-dev")
endif()

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

target_compile_definitions(cpp_components PUBLIC
  BOOST_ASIO_HAS_IO_URING=1
  BOOST_ASIO_DISABLE_EPOLL=1
)

target_link_libraries(cpp_components PRIVATE project_options)
target_link_libraries(cpp_components PUBLIC project_features)
target_link_libraries(cpp_components PUBLIC
  Boost::headers Boost::system OpenSSL::SSL CURL::libcurl
  ${URING_LIBRARY}
)
