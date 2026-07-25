/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_HTTPS_CLIENT_ASYNC_H
#define CPP_COMPONENTS_HTTPS_CLIENT_ASYNC_H

#include "executor/executor.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace cpp_components::https_client_async {

enum class HttpMethod {
    get,
    head,
    post,
    put,
    del,
    patch,
};

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    unsigned status_code = 0;
    std::string body;
    std::vector<HttpHeader> headers;
};

class HttpsClientAsync : public std::enable_shared_from_this<HttpsClientAsync> {
public:
    using ResponseHandler = std::function<void(const std::error_code &ec, HttpResponse response)>;

    static std::shared_ptr<HttpsClientAsync> create(executor::Executor &executor);

    HttpsClientAsync(const HttpsClientAsync &) = delete;
    HttpsClientAsync(HttpsClientAsync &&) = delete;
    HttpsClientAsync &operator=(const HttpsClientAsync &) = delete;
    HttpsClientAsync &operator=(HttpsClientAsync &&) = delete;

    ~HttpsClientAsync();

    void set_ca_certificate(const std::string &ca_certificate_file);
    void set_timeout(std::chrono::seconds timeout);

    void get(std::string host, std::string port, std::string target, ResponseHandler handler);
    void post(std::string host, std::string port, std::string target, std::string body,
        ResponseHandler handler);
    void request(HttpMethod method, std::string host, std::string port, std::string target,
        std::string body, std::vector<HttpHeader> headers, ResponseHandler handler);

    void cancel();
    bool is_busy() const;

private:
    using Tcp = boost::asio::ip::tcp;
    using SslStream = boost::asio::ssl::stream<boost::beast::tcp_stream>;

    enum class RequestState {
        idle,
        in_progress,
        cancelling,
    };

    explicit HttpsClientAsync(executor::Executor &executor);

    void do_request(HttpMethod method, std::string host, const std::string &port,
        const std::string &target, std::string body, const std::vector<HttpHeader> &headers,
        ResponseHandler handler);
    void do_cancel();

    void handle_resolve(std::uint64_t generation, const boost::system::error_code &ec,
        const Tcp::resolver::results_type &results);
    void handle_connect(std::uint64_t generation, const boost::system::error_code &ec,
        const Tcp::endpoint &endpoint);
    void handle_ssl_handshake(std::uint64_t generation, const boost::system::error_code &ec);
    void handle_write(std::uint64_t generation, const boost::system::error_code &ec,
        std::size_t bytes_transferred);
    void handle_read(std::uint64_t generation, const boost::system::error_code &ec,
        std::size_t bytes_transferred);

    void set_state(RequestState new_state);
    bool is_current_request(std::uint64_t generation) const;
    void create_stream();
    void destroy_stream();
    void cancel_pending_operations();
    void close_socket();
    void fail_request(const boost::system::error_code &ec);
    void complete_request(const std::error_code &ec, HttpResponse response);
    void apply_timeout();

    static boost::beast::http::verb to_beast_verb(HttpMethod method);

    executor::Executor &executor;
    boost::asio::ssl::context ssl_context;
    Tcp::resolver resolver;
    std::unique_ptr<SslStream> stream;
    boost::beast::flat_buffer read_buffer;
    boost::beast::http::request<boost::beast::http::string_body> http_request;
    boost::beast::http::response<boost::beast::http::string_body> http_response;
    ResponseHandler response_handler;
    std::string host;
    std::chrono::seconds timeout { 30 };
    std::atomic<RequestState> state { RequestState::idle };
    std::uint64_t request_generation = 0;
};

} // namespace cpp_components::https_client_async

#endif // CPP_COMPONENTS_HTTPS_CLIENT_ASYNC_H
