#include "balance/console.hpp"
#include <cstring>

namespace balance {
const char* logLevelName(LogLevel level) {
    switch (level) { case LogLevel::Quiet: return "QUIET"; case LogLevel::Normal: return "NORMAL"; case LogLevel::Debug: return "DEBUG"; }
    return "UNKNOWN";
}
ConsoleCommand SerialCommandParser::feed(char ch) {
    if (ch != '\r' && ch != '\n') {
        if ((ch == '\b' || ch == 127) && !overflow_) { if (used_) line_[--used_] = 0; return ConsoleCommand::None; }
        if ((static_cast<unsigned char>(ch) < 32 && ch != '\t') || static_cast<unsigned char>(ch) > 126) overflow_ = true;
        if (used_ < sizeof(line_) - 1 && !overflow_) line_[used_++] = ch;
        else overflow_ = true;
        return ConsoleCommand::None;
    }
    if (overflow_) { memset(line_, 0, sizeof(line_)); used_ = 0; overflow_ = false; return ConsoleCommand::TooLong; }
    while (used_ && (line_[used_ - 1] == ' ' || line_[used_ - 1] == '\t')) --used_;
    line_[used_] = 0;
    const char* begin = line_;
    while (*begin == ' ' || *begin == '\t') ++begin;
    ConsoleCommand command = ConsoleCommand::Unknown;
    if (*begin == 0) command = ConsoleCommand::None;
    else if (strcmp(begin, "status") == 0) command = ConsoleCommand::Status;
    else if (strcmp(begin, "help") == 0) command = ConsoleCommand::Help;
    else if (strcmp(begin, "log quiet") == 0) command = ConsoleCommand::Quiet;
    else if (strcmp(begin, "log normal") == 0) command = ConsoleCommand::Normal;
    else if (strcmp(begin, "log debug") == 0) command = ConsoleCommand::Debug;
    // Never retain/echo arbitrary input (a user might accidentally paste a secret).
    memset(line_, 0, sizeof(line_)); used_ = 0;
    return command;
}
}
