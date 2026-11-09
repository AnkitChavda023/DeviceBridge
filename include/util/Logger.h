#pragma once
#include <functional>
#include <string>

enum class LogLevel { Debug, Info, Warn, Error, Off };

class Logger {
public:
    typedef std::function<void(const std::string&)> Sink;

    static void setLevel(LogLevel level);
    static LogLevel level();
    static void setSink(Sink sink);
    static void log(LogLevel level, const std::string& deviceId, const std::string& message);
    static bool parseLevel(const std::string& text, LogLevel& out);
};
