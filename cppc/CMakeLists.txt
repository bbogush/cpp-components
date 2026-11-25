find_package(Boost CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

find_library(URING_LIBRARY NAMES uring)
if(NOT URING_LIBRARY)
  message(FATAL_ERROR "liburing not found; install liburing-dev")
endif()

# Keep quill's unprefixed LOG_* macros out of consumers; they clash with <syslog.h>.
set(QUILL_DISABLE_NON_PREFIXED_MACROS
    ON
    CACHE BOOL "")

include(FetchContent)
FetchContent_Declare(
  quill
  URL https://github.com/odygrd/quill/archive/refs/tags/v13.1.0.tar.gz
  URL_HASH SHA256=a08b93bafc7e712b28eb47e4a1ed9b60fc01ac7fad3ce269f1f100560a506a85
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SYSTEM)

FetchContent_MakeAvailable(quill)

add_library(
  cppc STATIC
  executor/executor.cpp
  timer/timer.cpp
  secure_websocket_client/secure_websocket_client.cpp
  secure_websocket_client/reconnecting_secure_websocket_client.cpp
  https_client_async/https_client_async.cpp
  https_multiplex_client/https_multiplex_client.cpp
  logger/logger.cpp)

add_library(cppc::cppc ALIAS cppc)

target_include_directories(cppc PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_definitions(cppc PUBLIC BOOST_ASIO_HAS_IO_URING=1 BOOST_ASIO_DISABLE_EPOLL=1)

target_link_libraries(cppc PRIVATE project_options)
target_link_libraries(cppc PUBLIC project_features)
target_link_libraries(cppc PUBLIC Boost::headers OpenSSL::SSL CURL::libcurl ${URING_LIBRARY}
                                  quill::quill)
