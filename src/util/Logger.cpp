#include "util/Logger.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include "util/Util.h"

namespace {

std::atomic<int>& levelValue() {
    static std::atomic<int> value(static_cast<int>(LogLevel::Warn));
    return value;
}

std::mutex& sinkMutex() {
    static std::mutex m;
    return m;
}

Logger::Sink& sinkValue() {
    static Logger::Sink sink;
    return sink;
}

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off: return "OFF";
    }
    return "?";
}

}  // namespace

void Logger::setLevel(LogLevel level) {
    levelValue() = static_cast<int>(level);
}

LogLevel Logger::level() {
    return static_cast<LogLevel>(levelValue().load());
}

void Logger::setSink(Sink sink) {
    std::lock_guard<std::mutex> lock(sinkMutex());
    sinkValue() = sink;
}

void Logger::log(LogLevel level, const std::string& deviceId, const std::string& message) {
    if (level < Logger::level() || Logger::level() == LogLevel::Off) {
        return;
    }
    std::string line = formatTimestamp(nowMs()) + " [" + levelName(level) + "] ";
    if (!deviceId.empty()) {
        line += "(" + deviceId + ") ";
    }
    line += message;

    std::lock_guard<std::mutex> lock(sinkMutex());
    if (sinkValue()) {
        sinkValue()(line);
    } else {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

bool Logger::parseLevel(const std::string& text, LogLevel& out) {
    std::string name = toLower(text);
    if (name == "debug") {
        out = LogLevel::Debug;
    } else if (name == "info") {
        out = LogLevel::Info;
    } else if (name == "warn") {
        out = LogLevel::Warn;
    } else if (name == "error") {
        out = LogLevel::Error;
    } else if (name == "off") {
        out = LogLevel::Off;
    } else {
        return false;
    }
    return true;
}
