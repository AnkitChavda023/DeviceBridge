#pragma once
#include <string>
#include <vector>
#include "core/DeviceError.h"

struct DeviceConfig {
    std::string id;
    std::string type;
    std::string transport = "virtual";
    std::string name;
    std::string path;
    int baud = 9600;
    int sampleIntervalMs = 200;
    int timeoutMs = 500;
    int rangeG = 2;
    int bufferCapacity = 256;
};

class ConfigManager {
public:
    static Result<std::vector<DeviceConfig> > loadFile(const std::string& path);
    static Result<std::vector<DeviceConfig> > parseText(const std::string& text);
    static Status validate(const DeviceConfig& config);
};
