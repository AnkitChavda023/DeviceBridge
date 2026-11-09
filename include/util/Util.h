#pragma once
#include <cstdint>
#include <string>
#include <vector>

long long nowMs();
long long steadyMs();
std::string formatTimestamp(long long ms);
std::string trim(const std::string& text);
std::string toLower(const std::string& text);
std::vector<std::string> split(const std::string& text, char separator);
bool parseInt(const std::string& text, int& out);
bool parseDouble(const std::string& text, double& out);
std::string toHex(const std::vector<uint8_t>& data);
std::vector<uint8_t> toBytes(const std::string& text);
