#pragma once
#include "balance/console.hpp"
#include <cstring>

namespace console_test {
balance::ConsoleCommand line(balance::SerialCommandParser& parser, const char* text) {
    balance::ConsoleCommand result = balance::ConsoleCommand::None;
    while (*text) { const auto next = parser.feed(*text++); if (next != balance::ConsoleCommand::None) result = next; }
    return result;
}
}
void test_serial_commands_are_framed_bounded_and_read_only() {
    using namespace balance;
    SerialCommandParser parser;
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::None), int(console_test::line(parser, "admin")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Unknown), int(console_test::line(parser, "\r\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Status), int(console_test::line(parser, " status \n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Help), int(console_test::line(parser, "help\r")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Quiet), int(console_test::line(parser, "log quiet\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Normal), int(console_test::line(parser, "log normal\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Debug), int(console_test::line(parser, "log debug\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Unknown), int(console_test::line(parser, "arm\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Unknown), int(console_test::line(parser, "admin extra\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Help), int(console_test::line(parser, "helx\bp\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::TooLong), int(console_test::line(parser, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffadmin\n")));
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::Status), int(console_test::line(parser, "status\n")));
    for (char ch : {'a', 'd', 'm', 'i', 'n', '\0', 'x'}) parser.feed(ch);
    TEST_ASSERT_EQUAL_INT(int(ConsoleCommand::TooLong), int(parser.feed('\n')));
}
void test_log_levels_never_filter_critical_safety_events() {
    using namespace balance;
    for (auto level : {LogLevel::Quiet, LogLevel::Normal, LogLevel::Debug}) {
        TEST_ASSERT_TRUE(logAllowed(level, LogKind::Critical));
        TEST_ASSERT_EQUAL(level != LogLevel::Quiet, logAllowed(level, LogKind::Transition));
        TEST_ASSERT_EQUAL(level == LogLevel::Debug, logAllowed(level, LogKind::Debug));
    }
    TEST_ASSERT_EQUAL_STRING("NORMAL", logLevelName(LogLevel::Normal));
}
