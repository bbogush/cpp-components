/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_HTTPS_MULTIPLEX_CLIENT_H
#define CPP_COMPONENTS_HTTPS_MULTIPLEX_CLIENT_H

#include "executor/executor.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace cpp_components::https_multiplex_client {

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

class HttpsMultiplexClient : public std::enable_shared_from_this<HttpsMultiplexClient> {
public:
    using ResponseHandler = std::function<void(const std::error_code &ec, HttpResponse response)>;

    static std::shared_ptr<HttpsMultiplexClient> create(executor::Executor &executor);

    HttpsMultiplexClient(const HttpsMultiplexClient &) = delete;
    HttpsMultiplexClient(HttpsMultiplexClient &&) = delete;
    HttpsMultiplexClient &operator=(const HttpsMultiplexClient &) = delete;
    HttpsMultiplexClient &operator=(HttpsMultiplexClient &&) = delete;

    ~HttpsMultiplexClient();

    void set_ca_certificate(const std::string &ca_certificate_file);
    void set_timeout(std::chrono::seconds timeout);

    void get(std::string host, std::string port, std::string target, ResponseHandler handler);
    void post(std::string host, std::string port, std::string target, std::string body,
        ResponseHandler handler);
    void request(HttpMethod method, std::string host, std::string port, std::string target,
        std::string body, std::vector<HttpHeader> headers, ResponseHandler handler);

    void cancel();
    std::size_t pending_count() const;
    bool is_busy() const;

private:
    struct ConnContext {
        std::shared_ptr<CURL> easy;
        std::shared_ptr<curl_slist> curl_headers;
        std::string url;
        std::string body;
        char error[CURL_ERROR_SIZE] {};
        std::string response_body;
        std::vector<HttpHeader> response_headers;
        ResponseHandler handler;
    };

    using TcpSocket = boost::asio::ip::tcp::socket;

    explicit HttpsMultiplexClient(executor::Executor &executor);

    void do_set_ca_certificate(std::string ca_certificate_file);
    void do_set_timeout(std::chrono::seconds timeout);
    void do_request(HttpMethod method, std::string host, const std::string &port,
        const std::string &target, std::string body, const std::vector<HttpHeader> &headers,
        ResponseHandler handler);
    void do_cancel();

    void start_request(std::shared_ptr<ConnContext> conn, HttpMethod method);
    std::error_code configure_easy_handle(const std::shared_ptr<ConnContext> &conn);
    std::error_code apply_method(CURL *easy, HttpMethod method, ConnContext &conn);

    static int socket_callback(CURL *easy, curl_socket_t socket, int what, void *userp,
        void *socketp);
    int handle_socket_event(curl_socket_t socket, int what, void *socketp);

    static int timer_callback(CURLM *multi, long timeout_ms, void *userp);
    int handle_timeout_event(long timeout_ms);

    int handle_add_socket(curl_socket_t socket, int what);
    int handle_remove_socket(int *actionp);
    int handle_set_socket(curl_socket_t socket, int what, int *actionp);
    void arm_socket_watches(const std::shared_ptr<TcpSocket> &tcp_socket, int action);

    void event_callback(const std::shared_ptr<TcpSocket> &tcp_socket, int action);
    void check_multi_info();
    void asio_timer_callback(const boost::system::error_code &error);

    static curl_socket_t open_socket_callback(void *clientp, curlsocktype purpose,
        struct curl_sockaddr *address);
    curl_socket_t handle_open_socket(curlsocktype purpose, struct curl_sockaddr *address);

    static int close_socket_callback(void *clientp, curl_socket_t item);
    int handle_close_socket(curl_socket_t item);

    static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata);

    static std::error_code from_curl_code(CURLcode code);
    static const char *to_curl_method(HttpMethod method);

    executor::Executor &executor;
    boost::asio::steady_timer timer;
    CURLM *multi = nullptr;
    int still_running = 0;
    std::string ca_certificate_file;
    std::chrono::seconds timeout { 15 };
    std::atomic<std::size_t> pending { 0 };
    std::unordered_map<curl_socket_t, std::shared_ptr<TcpSocket>> socket_map;
    std::unordered_map<curl_socket_t, int> socket_actions;
    std::unordered_map<ConnContext *, std::shared_ptr<ConnContext>> connections;
    std::unordered_map<int *, std::shared_ptr<int>> actions;
};

} // namespace cpp_components::https_multiplex_client

#endif // CPP_COMPONENTS_HTTPS_MULTIPLEX_CLIENT_H
