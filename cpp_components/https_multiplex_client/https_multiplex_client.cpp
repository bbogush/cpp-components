/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "https_multiplex_client.h"

#include <boost/asio/ip/tcp.hpp>

#include <cstring>
#include <mutex>
#include <utility>

namespace cpp_components::https_multiplex_client {

namespace {

namespace net = boost::asio;

void ensure_curl_global_init()
{
    static std::once_flag once;
    std::call_once(once, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        static const auto cleanup = std::shared_ptr<void>(nullptr,
            [](void *) { curl_global_cleanup(); });
        (void)cleanup;
    });
}

std::string trim_header_token(std::string value)
{
    while (!value.empty() &&
        (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' ||
            value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

} // namespace

std::shared_ptr<HttpsMultiplexClient> HttpsMultiplexClient::create(executor::Executor &executor)
{
    return std::shared_ptr<HttpsMultiplexClient>(new HttpsMultiplexClient(executor));
}

HttpsMultiplexClient::HttpsMultiplexClient(executor::Executor &executor) :
    executor(executor), timer(executor.get_context())
{
    ensure_curl_global_init();
    multi = curl_multi_init();
    if (!multi) {
        return;
    }

    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, socket_callback);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, timer_callback);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);
    curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
}

HttpsMultiplexClient::~HttpsMultiplexClient()
{
    timer.cancel();

    auto outstanding = std::move(connections);
    connections.clear();
    pending.store(0, std::memory_order_release);

    for (auto &entry : outstanding) {
        auto &conn = entry.second;
        if (multi && conn->easy) {
            curl_multi_remove_handle(multi, conn->easy.get());
        }
        conn->handler = nullptr;
    }

    for (auto &entry : socket_map) {
        boost::system::error_code ec;
        entry.second->cancel(ec);
        entry.second->close(ec);
    }
    socket_map.clear();
    socket_actions.clear();
    actions.clear();
    still_running = 0;

    if (multi) {
        curl_multi_cleanup(multi);
        multi = nullptr;
    }
}

void HttpsMultiplexClient::set_ca_certificate(const std::string &ca_certificate_file)
{
    auto self = shared_from_this();
    auto ca_certificate_handler = [self, ca_certificate_file]() {
        self->do_set_ca_certificate(ca_certificate_file);
    };
    executor.post(std::move(ca_certificate_handler));
}

void HttpsMultiplexClient::set_timeout(std::chrono::seconds timeout)
{
    auto self = shared_from_this();
    auto timeout_handler = [self, timeout]() { self->do_set_timeout(timeout); };
    executor.post(std::move(timeout_handler));
}

void HttpsMultiplexClient::get(std::string host, std::string port, std::string target,
    ResponseHandler handler)
{
    request(HttpMethod::get, std::move(host), std::move(port), std::move(target), {}, {},
        std::move(handler));
}

void HttpsMultiplexClient::post(std::string host, std::string port, std::string target,
    std::string body, ResponseHandler handler)
{
    request(HttpMethod::post, std::move(host), std::move(port), std::move(target), std::move(body),
        {}, std::move(handler));
}

void HttpsMultiplexClient::request(HttpMethod method, std::string host, std::string port,
    std::string target, std::string body, std::vector<HttpHeader> headers, ResponseHandler handler)
{
    auto self = shared_from_this();
    auto request_handler = [self, method, host = std::move(host), port = std::move(port),
                               target = std::move(target), body = std::move(body),
                               headers = std::move(headers),
                               handler = std::move(handler)]() mutable {
        self->do_request(method, host, port, target, std::move(body), headers,
            std::move(handler));
    };
    executor.post(std::move(request_handler));
}

void HttpsMultiplexClient::cancel()
{
    auto self = shared_from_this();
    executor.post([self]() { self->do_cancel(); });
}

std::size_t HttpsMultiplexClient::pending_count() const
{
    return pending.load(std::memory_order_acquire);
}

bool HttpsMultiplexClient::is_busy() const
{
    return pending_count() > 0;
}

void HttpsMultiplexClient::do_set_ca_certificate(std::string ca_certificate_file)
{
    this->ca_certificate_file = std::move(ca_certificate_file);
}

void HttpsMultiplexClient::do_set_timeout(std::chrono::seconds timeout)
{
    this->timeout = timeout;
}

void HttpsMultiplexClient::do_request(HttpMethod method, const std::string &host,
    const std::string &port, const std::string &target, std::string body,
    const std::vector<HttpHeader> &headers, ResponseHandler handler)
{
    if (!to_curl_method(method)) {
        if (handler) {
            handler(std::make_error_code(std::errc::invalid_argument), {});
        }
        return;
    }

    auto conn = std::make_shared<ConnContext>();
    conn->url = "https://" + host + ":" + port + target;
    conn->body = std::move(body);
    conn->handler = std::move(handler);

    if (!headers.empty()) {
        curl_slist *curl_headers = nullptr;
        for (const auto &header : headers) {
            const std::string line = header.name + ": " + header.value;
            curl_headers = curl_slist_append(curl_headers, line.c_str());
        }
        conn->curl_headers = std::shared_ptr<curl_slist>(curl_headers, curl_slist_free_all);
    }

    start_request(conn, method);
}

void HttpsMultiplexClient::start_request(const std::shared_ptr<ConnContext> &conn,
    HttpMethod method)
{
    if (!multi) {
        if (conn->handler) {
            conn->handler(std::make_error_code(std::errc::operation_not_permitted), {});
        }
        return;
    }

    CURL *easy = curl_easy_init();
    if (!easy) {
        if (conn->handler) {
            conn->handler(std::make_error_code(std::errc::resource_unavailable_try_again), {});
        }
        return;
    }
    conn->easy = std::shared_ptr<CURL>(easy, curl_easy_cleanup);

    std::error_code ec = apply_method(conn->easy.get(), method, *conn);
    if (ec) {
        if (conn->handler) {
            conn->handler(ec, {});
        }
        return;
    }

    ec = configure_easy_handle(conn);
    if (ec) {
        if (conn->handler) {
            conn->handler(ec, {});
        }
        return;
    }

    const CURLMcode ret = curl_multi_add_handle(multi, conn->easy.get());
    if (ret != CURLM_OK) {
        if (conn->handler) {
            conn->handler(std::make_error_code(std::errc::io_error), {});
        }
        return;
    }

    pending.fetch_add(1, std::memory_order_release);
    connections.insert({ conn.get(), conn });

    const CURLMcode action_ret = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0,
        &still_running);
    if (action_ret == CURLM_OK) {
        check_multi_info();
    }
}

void HttpsMultiplexClient::do_cancel()
{
    timer.cancel();

    auto outstanding = std::move(connections);
    connections.clear();

    for (auto &entry : outstanding) {
        auto &conn = entry.second;
        if (multi && conn->easy) {
            curl_multi_remove_handle(multi, conn->easy.get());
        }
        if (conn->handler) {
            auto handler = std::move(conn->handler);
            handler(std::make_error_code(std::errc::operation_canceled), {});
        }
    }

    pending.store(0, std::memory_order_release);

    for (auto &entry : socket_map) {
        boost::system::error_code ec;
        entry.second->cancel(ec);
        entry.second->close(ec);
    }
    socket_map.clear();
    socket_actions.clear();
    actions.clear();
    still_running = 0;
}

std::error_code HttpsMultiplexClient::configure_easy_handle(
    const std::shared_ptr<ConnContext> &conn)
{
    CURL *easy = conn->easy.get();
    std::memset(conn->error, 0, sizeof(conn->error));

    curl_easy_setopt(easy, CURLOPT_URL, conn->url.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, conn.get());
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, conn.get());
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, conn.get());
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);

    const long timeout_seconds = timeout.count() > 0 ? static_cast<long>(timeout.count()) : 0L;
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, timeout_seconds > 0 ? timeout_seconds : 5L);

    if (!ca_certificate_file.empty()) {
        curl_easy_setopt(easy, CURLOPT_CAINFO, ca_certificate_file.c_str());
    }

    if (conn->curl_headers) {
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, conn->curl_headers.get());
    }

    curl_easy_setopt(easy, CURLOPT_OPENSOCKETFUNCTION, open_socket_callback);
    curl_easy_setopt(easy, CURLOPT_OPENSOCKETDATA, this);
    curl_easy_setopt(easy, CURLOPT_CLOSESOCKETFUNCTION, close_socket_callback);
    curl_easy_setopt(easy, CURLOPT_CLOSESOCKETDATA, this);

    return {};
}

std::error_code HttpsMultiplexClient::apply_method(CURL *easy, HttpMethod method, ConnContext &conn)
{
    if (!to_curl_method(method)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    switch (method) {
    case HttpMethod::get:
        curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
        break;
    case HttpMethod::head:
        curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
        break;
    case HttpMethod::post:
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, conn.body.c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(conn.body.size()));
        break;
    case HttpMethod::put:
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, conn.body.c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(conn.body.size()));
        break;
    case HttpMethod::del:
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
        if (!conn.body.empty()) {
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, conn.body.c_str());
            curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(conn.body.size()));
        }
        break;
    case HttpMethod::patch:
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, conn.body.c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(conn.body.size()));
        break;
    default:
        return std::make_error_code(std::errc::invalid_argument);
    }

    return {};
}

int HttpsMultiplexClient::socket_callback(CURL *, curl_socket_t socket, int what, void *userp,
    void *socketp)
{
    auto *client = static_cast<HttpsMultiplexClient *>(userp);
    return client->handle_socket_event(socket, what, socketp);
}

int HttpsMultiplexClient::handle_socket_event(curl_socket_t socket, int what, void *socketp)
{
    auto *actionp = static_cast<int *>(socketp);
    if (what == CURL_POLL_REMOVE) {
        socket_actions.erase(socket);
        return handle_remove_socket(actionp);
    }
    if (!actionp) {
        return handle_add_socket(socket, what);
    }
    return handle_set_socket(socket, what, actionp);
}

int HttpsMultiplexClient::timer_callback(CURLM *, long timeout_ms, void *userp)
{
    auto *client = static_cast<HttpsMultiplexClient *>(userp);
    return client->handle_timeout_event(timeout_ms);
}

int HttpsMultiplexClient::handle_timeout_event(long timeout_ms)
{
    timer.cancel();
    if (timeout_ms < 0) {
        return 0;
    }

    timer.expires_after(std::chrono::milliseconds(timeout_ms));
    auto self = shared_from_this();
    auto timer_callback = [self](const boost::system::error_code &error) {
        self->asio_timer_callback(error);
    };
    timer.async_wait(std::move(timer_callback));
    return 0;
}

int HttpsMultiplexClient::handle_add_socket(curl_socket_t socket, int what)
{
    auto action = std::make_shared<int>(0);
    if (handle_set_socket(socket, what, action.get()) < 0) {
        return -1;
    }

    const CURLMcode ret = curl_multi_assign(multi, socket, action.get());
    if (ret != CURLM_OK) {
        return -1;
    }

    actions.insert({ action.get(), action });
    return 0;
}

int HttpsMultiplexClient::handle_remove_socket(int *actionp)
{
    if (actionp) {
        actions.erase(actionp);
    }
    return 0;
}

void HttpsMultiplexClient::arm_socket_watches(const std::shared_ptr<TcpSocket> &tcp_socket,
    int action)
{
    auto self = shared_from_this();
    if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) {
        auto read_callback = [self, tcp_socket](const boost::system::error_code &ec) {
            if (ec) {
                return;
            }
            self->event_callback(tcp_socket, CURL_CSELECT_IN);
        };
        tcp_socket->async_wait(TcpSocket::wait_read, std::move(read_callback));
    }
    if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {
        auto write_callback = [self, tcp_socket](const boost::system::error_code &ec) {
            if (ec) {
                return;
            }
            self->event_callback(tcp_socket, CURL_CSELECT_OUT);
        };
        tcp_socket->async_wait(TcpSocket::wait_write, std::move(write_callback));
    }
}

int HttpsMultiplexClient::handle_set_socket(curl_socket_t socket, int action, int *actionp)
{
    auto it = socket_map.find(socket);
    if (it == socket_map.end()) {
        return 0;
    }

    auto &tcp_socket = it->second;
    *actionp = action;
    socket_actions[socket] = action;

    boost::system::error_code cancel_ec;
    tcp_socket->cancel(cancel_ec);
    arm_socket_watches(tcp_socket, action);
    return 0;
}

void HttpsMultiplexClient::event_callback(const std::shared_ptr<TcpSocket> &tcp_socket, int action)
{
    if (!multi || !tcp_socket->is_open()) {
        return;
    }

    const curl_socket_t fd = tcp_socket->native_handle();
    const CURLMcode ret = curl_multi_socket_action(multi, fd, action, &still_running);
    if (ret != CURLM_OK) {
        return;
    }

    check_multi_info();
    if (still_running <= 0) {
        timer.cancel();
        return;
    }

    // async_wait is one-shot; re-arm when curl keeps the same poll interest.
    auto watch = socket_actions.find(fd);
    if (watch != socket_actions.end()) {
        arm_socket_watches(tcp_socket, watch->second);
    }
}

void HttpsMultiplexClient::check_multi_info()
{
    int messages_left = 0;
    while (CURLMsg *msg = curl_multi_info_read(multi, &messages_left)) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }

        CURL *easy = msg->easy_handle;
        CURLcode result = msg->data.result;

        ConnContext *raw_conn = nullptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &raw_conn);

        auto it = connections.find(raw_conn);
        if (it == connections.end()) {
            curl_multi_remove_handle(multi, easy);
            continue;
        }

        auto conn = it->second;
        connections.erase(it);
        curl_multi_remove_handle(multi, easy);
        pending.fetch_sub(1, std::memory_order_release);

        HttpResponse response;
        if (result == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<unsigned>(http_code);
            response.body = std::move(conn->response_body);
            response.headers = std::move(conn->response_headers);
        }

        if (conn->handler) {
            auto handler = std::move(conn->handler);
            handler(from_curl_code(result), std::move(response));
        }
    }
}

void HttpsMultiplexClient::asio_timer_callback(const boost::system::error_code &ec)
{
    if (ec || !multi) {
        return;
    }

    const CURLMcode ret = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &still_running);
    if (ret != CURLM_OK) {
        return;
    }
    check_multi_info();
}

curl_socket_t HttpsMultiplexClient::open_socket_callback(void *clientp, curlsocktype purpose,
    struct curl_sockaddr *address)
{
    auto *client = static_cast<HttpsMultiplexClient *>(clientp);
    return client->handle_open_socket(purpose, address);
}

curl_socket_t HttpsMultiplexClient::handle_open_socket(curlsocktype purpose,
    struct curl_sockaddr *address)
{
    if (purpose != CURLSOCKTYPE_IPCXN) {
        return CURL_SOCKET_BAD;
    }

    auto tcp_socket = std::make_shared<TcpSocket>(executor.get_context());
    boost::system::error_code ec;
    if (address->family == AF_INET) {
        tcp_socket->open(net::ip::tcp::v4(), ec);
    } else if (address->family == AF_INET6) {
        tcp_socket->open(net::ip::tcp::v6(), ec);
    } else {
        return CURL_SOCKET_BAD;
    }

    if (ec) {
        return CURL_SOCKET_BAD;
    }

    tcp_socket->non_blocking(true, ec);
    if (ec) {
        return CURL_SOCKET_BAD;
    }

    const curl_socket_t sockfd = tcp_socket->native_handle();
    socket_map.insert({ sockfd, std::move(tcp_socket) });
    return sockfd;
}

int HttpsMultiplexClient::close_socket_callback(void *clientp, curl_socket_t item)
{
    auto *client = static_cast<HttpsMultiplexClient *>(clientp);
    return client->handle_close_socket(item);
}

int HttpsMultiplexClient::handle_close_socket(curl_socket_t item)
{
    socket_actions.erase(item);
    auto it = socket_map.find(item);
    if (it == socket_map.end()) {
        return 0;
    }

    boost::system::error_code ec;
    it->second->cancel(ec);
    it->second->close(ec);
    socket_map.erase(it);
    return 0;
}

size_t HttpsMultiplexClient::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *conn = static_cast<ConnContext *>(userdata);
    const size_t written = size * nmemb;
    conn->response_body.append(ptr, written);
    return written;
}

size_t HttpsMultiplexClient::header_callback(char *buffer, size_t size, size_t nitems,
    void *userdata)
{
    auto *conn = static_cast<ConnContext *>(userdata);
    const size_t total = size * nitems;
    std::string line(buffer, total);
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return total;
    }

    HttpHeader header;
    header.name = trim_header_token(line.substr(0, colon));
    header.value = trim_header_token(line.substr(colon + 1));
    if (!header.name.empty()) {
        conn->response_headers.push_back(std::move(header));
    }
    return total;
}

std::error_code HttpsMultiplexClient::from_curl_code(CURLcode code)
{
    if (code == CURLE_OK) {
        return {};
    }
    if (code == CURLE_OPERATION_TIMEDOUT) {
        return std::make_error_code(std::errc::timed_out);
    }
    if (code == CURLE_COULDNT_RESOLVE_HOST) {
        return std::make_error_code(std::errc::host_unreachable);
    }
    if (code == CURLE_COULDNT_CONNECT) {
        return std::make_error_code(std::errc::connection_refused);
    }
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        return std::make_error_code(std::errc::operation_canceled);
    }
    return std::make_error_code(std::errc::io_error);
}

const char *HttpsMultiplexClient::to_curl_method(HttpMethod method)
{
    switch (method) {
    case HttpMethod::get:
        return "GET";
    case HttpMethod::head:
        return "HEAD";
    case HttpMethod::post:
        return "POST";
    case HttpMethod::put:
        return "PUT";
    case HttpMethod::del:
        return "DELETE";
    case HttpMethod::patch:
        return "PATCH";
    default:
        return nullptr;
    }
}

} // namespace cpp_components::https_multiplex_client
