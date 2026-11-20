/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_LOGGER_H
#define CPP_COMPONENTS_LOGGER_H

#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/StringRef.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace cpp_components::logger {

enum class Level : uint8_t {
    trace,
    debug,
    info,
    warning,
    error,
    critical,
};

// Bit flags; combine with |, e.g. Output::console | Output::file.
enum class Output : uint8_t {
    console = 1U << 0U,
    file = 1U << 1U,
};

constexpr Output operator|(Output lhs, Output rhs) noexcept
{
    return static_cast<Output>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr Output operator&(Output lhs, Output rhs) noexcept
{
    return static_cast<Output>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

// Passes a string argument to the log macros without copying it. The backend formats it later, so
// the string must outlive the log call and stay unchanged until the message is written.
using StringRef = quill::utility::StringRef;

struct Config {
    std::string name = "cpp_components";
    Output output = Output::console;
    std::filesystem::path file_path;
    size_t max_file_size = 0;
    uint32_t max_backup_files = 0;
    Level level = Level::info;
};

std::error_code init(const Config &config);
void uninit();
void flush_log();

namespace detail {

// Inline so the log macros avoid a function call.
inline constinit std::atomic<quill::Logger *> logger_instance { nullptr };

} // namespace detail

} // namespace cpp_components::logger

#define CPP_COMPONENTS_LOG_IMPL(quill_macro, ...)                                              \
    do {                                                                                       \
        quill::Logger *const cpp_components_logger =                                           \
            ::cpp_components::logger::detail::logger_instance.load(std::memory_order_acquire); \
        if (cpp_components_logger != nullptr) {                                                \
            quill_macro(cpp_components_logger, __VA_ARGS__);                                   \
        }                                                                                      \
    } while (0)

#define CPP_COMPONENTS_LOG_TRACE(...) CPP_COMPONENTS_LOG_IMPL(QUILL_LOG_TRACE, __VA_ARGS__)
#define CPP_COMPONENTS_LOG_DEBUG(...) CPP_COMPONENTS_LOG_IMPL(QUILL_LOG_DEBUG, __VA_ARGS__)
#define CPP_COMPONENTS_LOG_INFO(...) CPP_COMPONENTS_LOG_IMPL(QUILL_LOG_INFO, __VA_ARGS__)
#define CPP_COMPONENTS_LOG_WARN(...) CPP_COMPONENTS_LOG_IMPL(QUILL_LOG_WARNING, __VA_ARGS__)
#define CPP_COMPONENTS_LOG_ERROR(...) CPP_COMPONENTS_LOG_IMPL(QUILL_LOG_ERROR, __VA_ARGS__)
#define CPP_COMPONENTS_LOG_CRITICAL(...) CPP_COMPONENTS_LOG_IMPL(QUILL_LOG_CRITICAL, __VA_ARGS__)
#endif // CPP_COMPONENTS_LOGGER_H
