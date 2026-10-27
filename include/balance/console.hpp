#pragma once
#include <cstddef>
#include <cstdint>

namespace balance {
enum class LogLevel : uint8_t { Quiet, Normal, Debug };
enum class LogKind : uint8_t { Critical, Transition, Debug };
constexpr bool logAllowed(LogLevel level, LogKind kind) {
    return kind == LogKind::Critical || (kind == LogKind::Transition && level != LogLevel::Quiet) ||
           (kind == LogKind::Debug && level == LogLevel::Debug);
}
const char* logLevelName(LogLevel level);
enum class ConsoleCommand : uint8_t { None, Status, Help, Quiet, Normal, Debug, Unknown, TooLong };
class SerialCommandParser {
public:
    ConsoleCommand feed(char ch);
private:
    char line_[32]{};
    size_t used_ = 0;
    bool overflow_ = false;
};
}
