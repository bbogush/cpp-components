/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp_components/logger/logger.h"

#include <gtest/gtest.h>

#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

namespace logger = cpp_components::logger;

class LoggerTest : public testing::Test {
protected:
    void SetUp() override
    {
        const auto *test_info = testing::UnitTest::GetInstance()->current_test_info();
        dir = std::filesystem::temp_directory_path() /
            ("cpp_components_logger_test_" + std::to_string(getpid()) + "_" + test_info->name());
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }

    void TearDown() override
    {
        logger::uninit();
        std::filesystem::remove_all(dir);
    }

    logger::Config make_config(const std::string &file_name) const
    {
        logger::Config config;
        config.output = logger::Output::file;
        config.file_path = dir / file_name;
        return config;
    }

    static std::string read_file(const std::filesystem::path &path)
    {
        std::ifstream stream { path };
        return { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    }

    std::filesystem::path dir;
};

size_t count_occurrences(std::string_view text, std::string_view pattern)
{
    size_t count = 0;
    for (auto pos = text.find(pattern); pos != std::string_view::npos;
        pos = text.find(pattern, pos + pattern.size())) {
        ++count;
    }
    return count;
}

// 'd' matches any digit; other characters must match exactly.
bool matches_mask(std::string_view text, std::string_view mask)
{
    if (text.size() != mask.size()) {
        return false;
    }
    for (size_t i = 0; i < mask.size(); ++i) {
        const bool is_match = mask[i] == 'd' ?
            std::isdigit(static_cast<unsigned char>(text[i])) != 0 :
            text[i] == mask[i];
        if (!is_match) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_F(LoggerTest, macros_are_noop_when_not_initialized)
{
    CPP_COMPONENTS_LOG_INFO("dropped {}", 1);
    logger::uninit();
}

TEST_F(LoggerTest, default_config_logs_to_stdout)
{
    EXPECT_EQ(logger::Config {}.output, logger::Output::console);

    testing::internal::CaptureStdout();
    ASSERT_FALSE(logger::init(logger::Config {}));
    CPP_COMPONENTS_LOG_INFO("stdout message {}", 7);
    logger::uninit();
    const auto output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("LOG_INFO"), std::string::npos) << output;
    EXPECT_NE(output.find("stdout message 7"), std::string::npos) << output;
}

TEST_F(LoggerTest, writes_message_with_quill_default_format)
{
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_INFO("hello {}", 42);
    logger::uninit();

    const auto content = read_file(config.file_path);
    const std::string_view time_mask = "dd:dd:dd.ddddddddd [";
    const std::string_view suffix = " cpp_components hello 42\n";
    ASSERT_GT(content.size(), time_mask.size() + suffix.size()) << content;
    const std::string_view line = content;
    EXPECT_TRUE(matches_mask(line.substr(0, time_mask.size()), time_mask)) << content;
    EXPECT_NE(line.find("] logger_test.cpp:"), std::string_view::npos) << content;
    EXPECT_NE(line.find(" LOG_INFO "), std::string_view::npos) << content;
    EXPECT_TRUE(line.ends_with(suffix)) << content;
}

TEST_F(LoggerTest, writes_string_ref_argument)
{
    static const std::string text = "referenced text";
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_INFO("value {}", logger::StringRef { text });
    logger::uninit();

    EXPECT_NE(read_file(config.file_path).find("value referenced text"), std::string::npos);
}

TEST_F(LoggerTest, flush_log_writes_pending_messages)
{
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_INFO("flushed message");
    logger::flush_log();

    EXPECT_NE(read_file(config.file_path).find("flushed message"), std::string::npos);
}

TEST_F(LoggerTest, flush_log_is_noop_when_not_initialized)
{
    logger::flush_log();
}

TEST_F(LoggerTest, file_output_does_not_write_stdout)
{
    const auto config = make_config("app.log");

    testing::internal::CaptureStdout();
    ASSERT_FALSE(logger::init(config));
    CPP_COMPONENTS_LOG_INFO("file only message");
    logger::uninit();
    const auto output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(output.find("file only message"), std::string::npos) << output;
    EXPECT_NE(read_file(config.file_path).find("file only message"), std::string::npos);
}

TEST_F(LoggerTest, console_and_file_output_writes_both)
{
    auto config = make_config("app.log");
    config.output = logger::Output::console | logger::Output::file;

    testing::internal::CaptureStdout();
    ASSERT_FALSE(logger::init(config));
    CPP_COMPONENTS_LOG_INFO("both message");
    logger::uninit();
    const auto output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("both message"), std::string::npos) << output;
    EXPECT_NE(read_file(config.file_path).find("both message"), std::string::npos);
}

TEST_F(LoggerTest, info_level_filters_debug)
{
    auto config = make_config("app.log");
    config.level = logger::Level::info;
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_DEBUG("debug message");
    CPP_COMPONENTS_LOG_INFO("info message");
    CPP_COMPONENTS_LOG_WARN("warn message");
    CPP_COMPONENTS_LOG_ERROR("error message");
    logger::uninit();

    const auto content = read_file(config.file_path);
    EXPECT_EQ(content.find("debug message"), std::string::npos);
    EXPECT_NE(content.find(" LOG_INFO "), std::string::npos);
    EXPECT_NE(content.find(" LOG_WARNING "), std::string::npos);
    EXPECT_NE(content.find(" LOG_ERROR "), std::string::npos);
}

TEST_F(LoggerTest, debug_level_writes_debug)
{
    auto config = make_config("app.log");
    config.level = logger::Level::debug;
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_DEBUG("debug message");
    logger::uninit();

    EXPECT_NE(read_file(config.file_path).find(" LOG_DEBUG "), std::string::npos);
}

TEST_F(LoggerTest, warning_and_error_levels_filter_lower_levels)
{
    auto config = make_config("warning.log");
    config.level = logger::Level::warning;
    ASSERT_FALSE(logger::init(config));
    CPP_COMPONENTS_LOG_INFO("info message");
    CPP_COMPONENTS_LOG_WARN("warn message");
    logger::uninit();

    config = make_config("error.log");
    config.level = logger::Level::error;
    ASSERT_FALSE(logger::init(config));
    CPP_COMPONENTS_LOG_WARN("warn message");
    CPP_COMPONENTS_LOG_ERROR("error message");
    logger::uninit();

    const auto warning_content = read_file(dir / "warning.log");
    EXPECT_EQ(warning_content.find("info message"), std::string::npos);
    EXPECT_NE(warning_content.find("warn message"), std::string::npos);

    const auto error_content = read_file(dir / "error.log");
    EXPECT_EQ(error_content.find("warn message"), std::string::npos);
    EXPECT_NE(error_content.find("error message"), std::string::npos);
}

TEST_F(LoggerTest, init_twice_fails)
{
    ASSERT_FALSE(logger::init(make_config("app.log")));

    EXPECT_EQ(logger::init(make_config("other.log")), std::errc::invalid_argument);
}

TEST_F(LoggerTest, init_rejects_invalid_config)
{
    auto config = make_config("app.log");
    config.file_path.clear();
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);

    config = make_config("app.log");
    config.name.clear();
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);

    config = make_config("app.log");
    config.max_file_size = 511;
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);

    config = make_config("app.log");
    config.max_backup_files = 1;
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);

    config = make_config("app.log");
    config.output = logger::Output::console | logger::Output::file;
    config.file_path.clear();
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);

    config = make_config("app.log");
    config.output = static_cast<logger::Output>(42);
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);

    config = make_config("app.log");
    config.output = logger::Output {};
    EXPECT_EQ(logger::init(config), std::errc::invalid_argument);
}

TEST_F(LoggerTest, console_output_ignores_file_settings)
{
    logger::Config config;
    config.max_file_size = 511;
    config.max_backup_files = 1;
    EXPECT_FALSE(logger::init(config));
}

TEST_F(LoggerTest, init_fails_when_file_cannot_be_opened)
{
    const auto blocker = dir / "not_a_directory";
    std::ofstream { blocker } << "x";
    auto config = make_config("app.log");
    config.file_path = blocker / "app.log";

    EXPECT_EQ(logger::init(config), std::errc::io_error);

    // A failed init leaves the logger usable for a later init.
    config = make_config("app.log");
    EXPECT_FALSE(logger::init(config));
}

TEST_F(LoggerTest, uninit_allows_reinit_with_new_file)
{
    ASSERT_FALSE(logger::init(make_config("first.log")));
    CPP_COMPONENTS_LOG_INFO("first message");
    logger::uninit();
    CPP_COMPONENTS_LOG_INFO("dropped message");

    ASSERT_FALSE(logger::init(make_config("second.log")));
    CPP_COMPONENTS_LOG_INFO("second message");
    logger::uninit();

    const auto first = read_file(dir / "first.log");
    const auto second = read_file(dir / "second.log");
    EXPECT_NE(first.find("first message"), std::string::npos);
    EXPECT_EQ(first.find("dropped message"), std::string::npos);
    EXPECT_NE(second.find("second message"), std::string::npos);
    EXPECT_EQ(second.find("dropped message"), std::string::npos);
}

TEST_F(LoggerTest, reinit_appends_to_existing_file)
{
    ASSERT_FALSE(logger::init(make_config("app.log")));
    CPP_COMPONENTS_LOG_INFO("first message");
    logger::uninit();

    ASSERT_FALSE(logger::init(make_config("app.log")));
    CPP_COMPONENTS_LOG_INFO("second message");
    logger::uninit();

    const auto content = read_file(dir / "app.log");
    EXPECT_NE(content.find("first message"), std::string::npos);
    EXPECT_NE(content.find("second message"), std::string::npos);
}

TEST_F(LoggerTest, rotates_and_keeps_max_backup_files)
{
    constexpr size_t message_count = 200;
    auto config = make_config("app.log");
    config.max_file_size = 1024;
    config.max_backup_files = 2;
    ASSERT_FALSE(logger::init(config));

    for (size_t i = 0; i < message_count; ++i) {
        CPP_COMPONENTS_LOG_INFO("rotation message number {}", i);
    }
    logger::uninit();

    EXPECT_TRUE(std::filesystem::exists(dir / "app.log"));
    EXPECT_TRUE(std::filesystem::exists(dir / "app.1.log"));
    EXPECT_TRUE(std::filesystem::exists(dir / "app.2.log"));
    EXPECT_FALSE(std::filesystem::exists(dir / "app.3.log"));
    EXPECT_LE(std::filesystem::file_size(dir / "app.1.log"), config.max_file_size);
    EXPECT_NE(read_file(dir / "app.log").find("rotation message number 199"), std::string::npos);
}

TEST_F(LoggerTest, does_not_rotate_by_default)
{
    constexpr size_t message_count = 200;
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    for (size_t i = 0; i < message_count; ++i) {
        CPP_COMPONENTS_LOG_INFO("unrotated message number {}", i);
    }
    logger::uninit();

    const auto content = read_file(dir / "app.log");
    EXPECT_NE(content.find("unrotated message number 0"), std::string::npos);
    EXPECT_NE(content.find("unrotated message number 199"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(dir / "app.1.log"));
}

TEST_F(LoggerTest, rotates_without_backups_when_max_backup_files_is_zero)
{
    constexpr size_t message_count = 200;
    auto config = make_config("app.log");
    config.max_file_size = 1024;
    ASSERT_FALSE(logger::init(config));

    for (size_t i = 0; i < message_count; ++i) {
        CPP_COMPONENTS_LOG_INFO("rotation message number {}", i);
    }
    logger::uninit();

    EXPECT_FALSE(std::filesystem::exists(dir / "app.1.log"));
    EXPECT_LE(std::filesystem::file_size(dir / "app.log"), config.max_file_size);
    EXPECT_NE(read_file(dir / "app.log").find("rotation message number 199"), std::string::npos);
}

TEST_F(LoggerTest, trace_level_writes_trace)
{
    auto config = make_config("app.log");
    config.level = logger::Level::trace;
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_TRACE("trace message");
    logger::uninit();

    EXPECT_NE(read_file(config.file_path).find("trace message"), std::string::npos);
}

TEST_F(LoggerTest, preallocate_succeeds_with_and_without_init)
{
    EXPECT_FALSE(logger::preallocate());

    ASSERT_FALSE(logger::init(make_config("app.log")));
    EXPECT_FALSE(logger::preallocate());
}

TEST_F(LoggerTest, preallocate_on_new_thread_keeps_logging)
{
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    std::error_code error;
    std::thread thread { [&error] {
        error = logger::preallocate();
        CPP_COMPONENTS_LOG_INFO("preallocated thread message");
    } };
    thread.join();
    logger::uninit();

    EXPECT_FALSE(error);
    EXPECT_NE(read_file(config.file_path).find("preallocated thread message"), std::string::npos);
}

TEST_F(LoggerTest, limit_macros_are_noop_when_not_initialized)
{
    CPP_COMPONENTS_LOG_WARN_LIMIT(std::chrono::hours { 1 }, "dropped {}", 1);
}

TEST_F(LoggerTest, limit_macros_write_every_level)
{
    auto config = make_config("app.log");
    config.level = logger::Level::trace;
    ASSERT_FALSE(logger::init(config));

    constexpr auto interval = std::chrono::hours { 1 };
    CPP_COMPONENTS_LOG_TRACE_LIMIT(interval, "trace limited");
    CPP_COMPONENTS_LOG_DEBUG_LIMIT(interval, "debug limited");
    CPP_COMPONENTS_LOG_INFO_LIMIT(interval, "info limited");
    CPP_COMPONENTS_LOG_WARN_LIMIT(interval, "warn limited");
    CPP_COMPONENTS_LOG_ERROR_LIMIT(interval, "error limited");
    CPP_COMPONENTS_LOG_CRITICAL_LIMIT(interval, "critical limited");
    logger::uninit();

    const auto content = read_file(config.file_path);
    EXPECT_NE(content.find("trace limited (1x)"), std::string::npos) << content;
    EXPECT_NE(content.find(" LOG_DEBUG "), std::string::npos) << content;
    EXPECT_NE(content.find("debug limited (1x)"), std::string::npos) << content;
    EXPECT_NE(content.find("info limited (1x)"), std::string::npos) << content;
    EXPECT_NE(content.find(" LOG_WARNING "), std::string::npos) << content;
    EXPECT_NE(content.find("warn limited (1x)"), std::string::npos) << content;
    EXPECT_NE(content.find("error limited (1x)"), std::string::npos) << content;
    EXPECT_NE(content.find(" LOG_CRITICAL "), std::string::npos) << content;
    EXPECT_NE(content.find("critical limited (1x)"), std::string::npos) << content;
}

TEST_F(LoggerTest, limit_macro_respects_level)
{
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    CPP_COMPONENTS_LOG_DEBUG_LIMIT(std::chrono::hours { 1 }, "debug limited");
    logger::uninit();

    EXPECT_EQ(read_file(config.file_path).find("debug limited"), std::string::npos);
}

TEST_F(LoggerTest, limit_macro_drops_calls_within_interval)
{
    constexpr size_t call_count = 5;
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    for (size_t i = 0; i < call_count; ++i) {
        CPP_COMPONENTS_LOG_WARN_LIMIT(std::chrono::hours { 1 }, "limited message {}", i);
    }
    logger::uninit();

    const auto content = read_file(config.file_path);
    EXPECT_EQ(count_occurrences(content, "limited message"), 1U) << content;
    EXPECT_NE(content.find("limited message 0 (1x)"), std::string::npos) << content;
}

TEST_F(LoggerTest, limit_macro_reports_dropped_calls_after_interval)
{
    constexpr size_t burst_count = 3;
    constexpr auto interval = std::chrono::milliseconds { 200 };
    const auto config = make_config("app.log");
    ASSERT_FALSE(logger::init(config));

    for (size_t i = 0; i < burst_count + 1; ++i) {
        if (i == burst_count) {
            std::this_thread::sleep_for(interval + std::chrono::milliseconds { 50 });
        }
        CPP_COMPONENTS_LOG_WARN_LIMIT(interval, "limited message {}", i);
    }
    logger::uninit();

    const auto content = read_file(config.file_path);
    EXPECT_EQ(count_occurrences(content, "limited message"), 2U) << content;
    EXPECT_NE(content.find("limited message 0 (1x)"), std::string::npos) << content;
    EXPECT_NE(content.find("limited message 3 (3x)"), std::string::npos) << content;
}
