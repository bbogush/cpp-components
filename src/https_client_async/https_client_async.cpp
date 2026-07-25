/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "https_client_async.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <utility>

namespace cpp_components::https_client_async {

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;

} // namespace

std::shared_ptr<HttpsClientAsync> HttpsClientAsync::create(executor::Executor &executor)
{
    return std::shared_ptr<HttpsClientAsync>(new HttpsClientAsync(executor));
}

HttpsClientAsync::HttpsClientAsync(executor::Executor &executor) :
    executor(executor), ssl_context(ssl::context::tlsv12_client), resolver(executor.get_context())
{
    ssl_context.set_default_verify_paths();
    ssl_context.set_verify_mode(ssl::verify_peer);
}

HttpsClientAsync::~HttpsClientAsync()
{
    do_cancel();
}

void HttpsClientAsync::set_ca_certificate(const std::string &ca_certificate_file)
{
    if (ca_certificate_file.empty()) {
        return;
    }

    ssl_context.load_verify_file(ca_certificate_file);
}

void HttpsClientAsync::set_timeout(std::chrono::seconds timeout)
{
    auto self = shared_from_this();
    auto timeout_handler = [self, timeout]() { self->timeout = timeout; };
    executor.post(std::move(timeout_handler));
}

void HttpsClientAsync::get(std::string host, std::string port, std::string target,
    ResponseHandler handler)
{
    request(HttpMethod::get, std::move(host), std::move(port), std::move(target), {}, {},
        std::move(handler));
}

void HttpsClientAsync::post(std::string host, std::string port, std::string target,
    std::string body, ResponseHandler handler)
{
    request(HttpMethod::post, std::move(host), std::move(port), std::move(target), std::move(body),
        {}, std::move(handler));
}

void HttpsClientAsync::request(HttpMethod method, std::string host, std::string port,
    std::string target, std::string body, std::vector<HttpHeader> headers, ResponseHandler handler)
{
    auto self = shared_from_this();
    auto request_handler = [self, method, host = std::move(host), port = std::move(port),
                               target = std::move(target), body = std::move(body),
                               headers = std::move(headers),
                               handler = std::move(handler)]() mutable {
        self->do_request(method, std::move(host), port, target, std::move(body), headers,
            std::move(handler));
    };
    executor.post(std::move(request_handler));
}

void HttpsClientAsync::cancel()
{
    auto self = shared_from_this();
    auto cancel_handler = [self]() { self->do_cancel(); };
    executor.post(std::move(cancel_handler));
}

bool HttpsClientAsync::is_busy() const
{
    return state.load(std::memory_order_acquire) != RequestState::idle;
}

void HttpsClientAsync::do_request(HttpMethod method, std::string host, const std::string &port,
    const std::string &target, std::string body, const std::vector<HttpHeader> &headers,
    ResponseHandler handler)
{
    if (state.load(std::memory_order_acquire) != RequestState::idle) {
        if (handler) {
            handler(std::make_error_code(std::errc::operation_in_progress), {});
        }
        return;
    }

    set_state(RequestState::in_progress);
    const auto generation = ++request_generation;
    this->host = std::move(host);
    response_handler = std::move(handler);

    http_request = {};
    http_request.version(11);
    http_request.method(to_beast_verb(method));
    http_request.target(target);
    http_request.set(http::field::host, this->host);
    http_request.set(http::field::user_agent, "https-client");
    for (const auto &header : headers) {
        http_request.set(header.name, header.value);
    }
    http_request.body() = std::move(body);
    http_request.prepare_payload();

    http_response = {};
    create_stream();

    auto self = shared_from_this();
    auto resolve_handler = [self, generation](const boost::system::error_code &ec,
                               const Tcp::resolver::results_type &results) {
        self->handle_resolve(generation, ec, results);
    };
    resolver.async_resolve(this->host, port, std::move(resolve_handler));
}

void HttpsClientAsync::do_cancel()
{
    const auto current_state = state.load(std::memory_order_acquire);
    if (current_state == RequestState::idle || current_state == RequestState::cancelling) {
        return;
    }

    set_state(RequestState::cancelling);
    ++request_generation;
    cancel_pending_operations();
    close_socket();
    destroy_stream();
    complete_request(std::make_error_code(std::errc::operation_canceled), {});
}

void HttpsClientAsync::handle_resolve(std::uint64_t generation, const boost::system::error_code &ec,
    const Tcp::resolver::results_type &results)
{
    if (!is_current_request(generation) || !stream) {
        return;
    }

    if (ec) {
        fail_request(ec);
        return;
    }

    apply_timeout();
    auto self = shared_from_this();
    auto connect_handler = [self, generation](const boost::system::error_code &connect_ec,
                               const Tcp::endpoint &endpoint) {
        self->handle_connect(generation, connect_ec, endpoint);
    };
    beast::get_lowest_layer(*stream).async_connect(results, std::move(connect_handler));
}

void HttpsClientAsync::handle_connect(std::uint64_t generation, const boost::system::error_code &ec,
    const Tcp::endpoint &)
{
    if (!is_current_request(generation) || !stream) {
        return;
    }

    if (ec) {
        fail_request(ec);
        return;
    }

    apply_timeout();

    if (!SSL_set_tlsext_host_name(stream->native_handle(), host.c_str())) {
        fail_request(boost::system::error_code(static_cast<int>(::ERR_get_error()),
            boost::asio::error::get_ssl_category()));
        return;
    }

    stream->set_verify_callback(ssl::host_name_verification(host));

    auto self = shared_from_this();
    auto ssl_handshake_handler = [self, generation](const boost::system::error_code &ssl_ec) {
        self->handle_ssl_handshake(generation, ssl_ec);
    };
    stream->async_handshake(ssl::stream_base::client, std::move(ssl_handshake_handler));
}

void HttpsClientAsync::handle_ssl_handshake(std::uint64_t generation,
    const boost::system::error_code &ec)
{
    if (!is_current_request(generation) || !stream) {
        return;
    }

    if (ec) {
        fail_request(ec);
        return;
    }

    apply_timeout();
    auto self = shared_from_this();
    auto write_handler = [self, generation](const boost::system::error_code &write_ec,
                             std::size_t bytes_transferred) {
        self->handle_write(generation, write_ec, bytes_transferred);
    };
    http::async_write(*stream, http_request, std::move(write_handler));
}

void HttpsClientAsync::handle_write(std::uint64_t generation, const boost::system::error_code &ec,
    std::size_t)
{
    if (!is_current_request(generation) || !stream) {
        return;
    }

    if (ec) {
        fail_request(ec);
        return;
    }

    apply_timeout();
    auto self = shared_from_this();
    auto read_handler = [self, generation](const boost::system::error_code &read_ec,
                            std::size_t bytes_transferred) {
        self->handle_read(generation, read_ec, bytes_transferred);
    };
    http::async_read(*stream, read_buffer, http_response, std::move(read_handler));
}

void HttpsClientAsync::handle_read(std::uint64_t generation, const boost::system::error_code &ec,
    std::size_t)
{
    if (!is_current_request(generation) || !stream) {
        return;
    }

    if (ec) {
        fail_request(ec);
        return;
    }

    HttpResponse response;
    response.status_code = http_response.result_int();
    response.body = std::move(http_response.body());
    for (const auto &field : http_response) {
        response.headers.push_back(
            HttpHeader { std::string(field.name_string()), std::string(field.value()) });
    }

    beast::get_lowest_layer(*stream).expires_never();
    close_socket();
    destroy_stream();
    complete_request({}, std::move(response));
}

void HttpsClientAsync::set_state(RequestState new_state)
{
    state.store(new_state, std::memory_order_release);
}

bool HttpsClientAsync::is_current_request(std::uint64_t generation) const
{
    return state.load(std::memory_order_acquire) == RequestState::in_progress &&
        generation == request_generation;
}

void HttpsClientAsync::create_stream()
{
    destroy_stream();
    read_buffer.consume(read_buffer.size());
    stream = std::make_unique<SslStream>(executor.get_context(), ssl_context);
}

void HttpsClientAsync::destroy_stream()
{
    stream.reset();
}

void HttpsClientAsync::cancel_pending_operations()
{
    resolver.cancel();
    if (stream) {
        beast::get_lowest_layer(*stream).cancel();
    }
}

void HttpsClientAsync::close_socket()
{
    if (!stream) {
        return;
    }

    boost::system::error_code ec;
    beast::get_lowest_layer(*stream).socket().close(ec);
}

void HttpsClientAsync::fail_request(const boost::system::error_code &ec)
{
    cancel_pending_operations();
    close_socket();
    destroy_stream();
    complete_request(static_cast<std::error_code>(ec), {});
}

void HttpsClientAsync::complete_request(const std::error_code &ec, HttpResponse response)
{
    auto handler = std::move(response_handler);
    response_handler = nullptr;
    host.clear();
    http_request = {};
    http_response = {};
    set_state(RequestState::idle);
    if (handler) {
        handler(ec, std::move(response));
    }
}

void HttpsClientAsync::apply_timeout()
{
    if (!stream || timeout <= std::chrono::seconds::zero()) {
        return;
    }

    beast::get_lowest_layer(*stream).expires_after(timeout);
}

boost::beast::http::verb HttpsClientAsync::to_beast_verb(HttpMethod method)
{
    switch (method) {
    case HttpMethod::get:
        return http::verb::get;
    case HttpMethod::head:
        return http::verb::head;
    case HttpMethod::post:
        return http::verb::post;
    case HttpMethod::put:
        return http::verb::put;
    case HttpMethod::del:
        return http::verb::delete_;
    case HttpMethod::patch:
        return http::verb::patch;
    default:
        return http::verb::get;
    }
}

} // namespace cpp_components::https_client_async
