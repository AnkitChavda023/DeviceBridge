#include "util/Util.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

long long steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string formatTimestamp(long long ms) {
    time_t seconds = static_cast<time_t>(ms / 1000);
    struct tm parts;
    gmtime_r(&seconds, &parts);
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", parts.tm_hour, parts.tm_min,
                  parts.tm_sec, static_cast<int>(ms % 1000));
    return buffer;
}

std::string trim(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        start++;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        end--;
    }
    return text.substr(start, end - start);
}

std::string toLower(const std::string& text) {
    std::string result = text;
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    }
    return result;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current += text[i];
        }
    }
    parts.push_back(current);
    return parts;
}

bool parseInt(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0' || value > 2000000000L || value < -2000000000L) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool parseDouble(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (*end != '\0') {
        return false;
    }
    out = value;
    return true;
}

std::string toHex(const std::vector<uint8_t>& data) {
    std::string result;
    char buffer[4];
    for (size_t i = 0; i < data.size(); i++) {
        std::snprintf(buffer, sizeof(buffer), "%02x", data[i]);
        if (i > 0) {
            result += " ";
        }
        result += buffer;
    }
    return result;
}

std::vector<uint8_t> toBytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}
