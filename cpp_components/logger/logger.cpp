/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "logger.h"

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/core/LogLevel.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>

#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace cpp_components::logger {

namespace {

constexpr size_t min_file_size = 512;

quill::LogLevel to_quill_level(Level level)
{
    switch (level) {
    case Level::trace:
        return quill::LogLevel::TraceL1;
    case Level::debug:
        return quill::LogLevel::Debug;
    case Level::info:
        return quill::LogLevel::Info;
    case Level::warning:
        return quill::LogLevel::Warning;
    case Level::error:
        return quill::LogLevel::Error;
    case Level::critical:
        return quill::LogLevel::Critical;
    }
    return quill::LogLevel::Info;
}

constexpr Output all_outputs = Output::console | Output::file;

bool has_console(Output output)
{
    return (output & Output::console) == Output::console;
}

bool has_file(Output output)
{
    return (output & Output::file) == Output::file;
}

bool is_valid_output(Output output)
{
    return output != Output {} && (output & all_outputs) == output;
}

bool is_valid(const Config &config)
{
    if (config.name.empty() || !is_valid_output(config.output)) {
        return false;
    }
    if (!has_file(config.output)) {
        return true;
    }
    if (config.file_path.empty()) {
        return false;
    }
    if (config.max_file_size == 0) {
        return config.max_backup_files == 0;
    }
    return config.max_file_size >= min_file_size;
}

quill::RotatingFileSinkConfig make_sink_config(const Config &config)
{
    quill::RotatingFileSinkConfig sink_config;
    sink_config.set_open_mode('a');
    if (config.max_file_size != 0) {
        sink_config.set_rotation_max_file_size(config.max_file_size);
        sink_config.set_max_backup_files(config.max_backup_files);
        sink_config.set_overwrite_rolled_files(true);
        sink_config.set_rotation_naming_scheme(
            quill::RotatingFileSinkConfig::RotationNamingScheme::Index);
    }
    return sink_config;
}

quill::Logger *create_logger(const Config &config)
{
    quill::Backend::start();

    std::vector<std::shared_ptr<quill::Sink>> sinks;
    if (has_console(config.output)) {
        sinks.push_back(quill::Frontend::create_or_get_sink<quill::ConsoleSink>("stdout"));
    }
    if (has_file(config.output)) {
        sinks.push_back(quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
            config.file_path.string(), make_sink_config(config)));
    }

    quill::Logger *logger = quill::Frontend::create_logger(config.name, std::move(sinks));
    logger->set_log_level(to_quill_level(config.level));
    return logger;
}

} // namespace

std::error_code init(const Config &config)
{
    if (!is_valid(config) || detail::logger_instance.load(std::memory_order_acquire) != nullptr) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // Quill reports failures only through exceptions
    try {
        if (quill::Frontend::get_logger(config.name) != nullptr) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        detail::logger_instance.store(create_logger(config), std::memory_order_release);
    } catch (const std::exception &) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

void uninit()
{
    quill::Logger *logger = detail::logger_instance.exchange(nullptr, std::memory_order_acq_rel);
    if (logger == nullptr) {
        return;
    }

    // Backend::stop() is not called: the backend is shared by all quill loggers in the process,
    // and quill already stops it at exit. A later init() reuses the running backend.
    try {
        logger->flush_log();
        quill::Frontend::remove_logger_blocking(logger);
    } catch (const std::exception &) {
    }
}

} // namespace cpp_components::logger
