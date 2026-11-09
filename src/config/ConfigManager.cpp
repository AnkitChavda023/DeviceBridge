#include "config/ConfigManager.h"
#include <fstream>
#include <set>
#include <sstream>
#include "core/DeviceInfo.h"
#include "devices/Parsers.h"
#include "manager/DeviceRegistry.h"
#include "transport/TtyTransport.h"
#include "util/Util.h"

typedef Result<std::vector<DeviceConfig> > ConfigList;

static std::string unquote(const std::string& value) {
    if (value.size() >= 2 && ((value[0] == '"' && value[value.size() - 1] == '"') ||
                              (value[0] == '\'' && value[value.size() - 1] == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

static Status setField(DeviceConfig& config, const std::string& key, const std::string& value) {
    int number = 0;
    if (key == "id") {
        config.id = value;
    } else if (key == "type") {
        config.type = value;
    } else if (key == "transport") {
        config.transport = value;
    } else if (key == "name") {
        config.name = value;
    } else if (key == "path") {
        config.path = value;
    } else if (key == "baud" || key == "sample_interval_ms" || key == "timeout_ms" || key == "range_g" ||
               key == "buffer_capacity") {
        if (!parseInt(value, number)) {
            return Status::failure(ErrorCode::InvalidArgument, key + " must be a number");
        }
        if (key == "baud") {
            config.baud = number;
        } else if (key == "sample_interval_ms") {
            config.sampleIntervalMs = number;
        } else if (key == "timeout_ms") {
            config.timeoutMs = number;
        } else if (key == "buffer_capacity") {
            config.bufferCapacity = number;
        } else {
            config.rangeG = number;
        }
    } else {
        return Status::failure(ErrorCode::Unsupported, "unknown setting '" + key + "'");
    }
    return Status::success();
}

Status ConfigManager::validate(const DeviceConfig& config) {
    if (!DeviceRegistry::isValidId(config.id)) {
        return Status::failure(ErrorCode::InvalidArgument, "invalid or missing id '" + config.id + "'");
    }
    DeviceType type;
    if (!parseDeviceType(config.type, type) || type == DeviceType::Serial) {
        return Status::failure(ErrorCode::InvalidArgument, "unsupported type '" + config.type + "' for " + config.id);
    }
    if (config.transport != "virtual" && config.transport != "chardev" && config.transport != "tty") {
        return Status::failure(ErrorCode::InvalidArgument, "unsupported transport '" + config.transport + "'");
    }
    if (config.transport != "virtual" && config.path.empty()) {
        return Status::failure(ErrorCode::InvalidArgument, "transport " + config.transport + " needs a path");
    }
    if (config.transport == "tty" && !TtyTransport::isSupportedBaud(config.baud)) {
        return Status::failure(ErrorCode::InvalidArgument, "unsupported baud rate");
    }
    if (config.sampleIntervalMs < 10 || config.sampleIntervalMs > 60000) {
        return Status::failure(ErrorCode::InvalidArgument, "sample_interval_ms must be 10..60000");
    }
    if (config.timeoutMs < 10 || config.timeoutMs > 10000) {
        return Status::failure(ErrorCode::InvalidArgument, "timeout_ms must be 10..10000");
    }
    if (config.bufferCapacity < 16 || config.bufferCapacity > 4096) {
        return Status::failure(ErrorCode::InvalidArgument, "buffer_capacity must be 16..4096");
    }
    if (!isValidAccelRange(config.rangeG)) {
        return Status::failure(ErrorCode::InvalidArgument, "range_g must be 2, 4, 8 or 16");
    }
    return Status::success();
}

ConfigList ConfigManager::parseText(const std::string& text) {
    std::vector<DeviceConfig> configs;
    std::istringstream stream(text);
    std::string rawLine;
    int lineNumber = 0;
    bool inEntry = false;

    while (std::getline(stream, rawLine)) {
        lineNumber++;
        size_t hash = rawLine.find('#');
        if (hash != std::string::npos) {
            rawLine = rawLine.substr(0, hash);
        }
        std::string line = trim(rawLine);
        if (line.empty() || line == "devices:") {
            continue;
        }
        if (line[0] == '-') {
            configs.push_back(DeviceConfig());
            inEntry = true;
            line = trim(line.substr(1));
            if (line.empty()) {
                continue;
            }
        }
        size_t colon = line.find(':');
        if (!inEntry || colon == std::string::npos) {
            return ConfigList::failure(ErrorCode::InvalidArgument, "line " + std::to_string(lineNumber) + ": cannot parse '" + line + "'");
        }
        std::string key = trim(line.substr(0, colon));
        std::string value = unquote(trim(line.substr(colon + 1)));
        Status status = setField(configs.back(), key, value);
        if (!status.ok()) {
            return ConfigList::failure(status.code, "line " + std::to_string(lineNumber) + ": " + status.message);
        }
    }

    std::set<std::string> seen;
    for (size_t i = 0; i < configs.size(); i++) {
        Status status = validate(configs[i]);
        if (!status.ok()) {
            return ConfigList::fromStatus(status);
        }
        if (!seen.insert(configs[i].id).second) {
            return ConfigList::failure(ErrorCode::AlreadyExists, "duplicate id '" + configs[i].id + "' in configuration");
        }
        if (configs[i].name.empty()) {
            configs[i].name = configs[i].id;
        }
    }
    return ConfigList::success(configs);
}

ConfigList ConfigManager::loadFile(const std::string& path) {
    std::ifstream file(path.c_str());
    if (!file) {
        return ConfigList::failure(ErrorCode::NotFound, "cannot open configuration file " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseText(buffer.str());
}
