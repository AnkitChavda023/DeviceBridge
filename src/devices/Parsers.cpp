#include "devices/Parsers.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "util/Util.h"

uint8_t xorChecksum(const uint8_t* data, size_t count) {
    uint8_t result = 0;
    for (size_t i = 0; i < count; i++) {
        result ^= data[i];
    }
    return result;
}

bool isValidAccelRange(int rangeG) {
    return rangeG == 2 || rangeG == 4 || rangeG == 8 || rangeG == 16;
}

static int16_t clampToInt16(double value) {
    if (value > 32767.0) {
        return 32767;
    }
    if (value < -32768.0) {
        return -32768;
    }
    return static_cast<int16_t>(std::lround(value));
}

static int16_t readInt16(const std::vector<uint8_t>& frame, size_t offset) {
    uint16_t raw = static_cast<uint16_t>((frame[offset] << 8) | frame[offset + 1]);
    return static_cast<int16_t>(raw);
}

std::vector<uint8_t> buildTemperatureFrame(double celsius) {
    int16_t raw = clampToInt16(celsius * 100.0);
    uint16_t bits = static_cast<uint16_t>(raw);
    uint8_t high = static_cast<uint8_t>(bits >> 8);
    uint8_t low = static_cast<uint8_t>(bits & 0xFF);
    std::vector<uint8_t> frame;
    frame.push_back(kSyncByte);
    frame.push_back(high);
    frame.push_back(low);
    frame.push_back(static_cast<uint8_t>(high ^ low));
    return frame;
}

std::vector<uint8_t> buildAccelFrame(double x, double y, double z, int rangeG) {
    double scale = 32768.0 / rangeG;
    double axes[3] = {x, y, z};
    std::vector<uint8_t> frame;
    frame.push_back(kSyncByte);
    for (int i = 0; i < 3; i++) {
        uint16_t bits = static_cast<uint16_t>(clampToInt16(axes[i] * scale));
        frame.push_back(static_cast<uint8_t>(bits >> 8));
        frame.push_back(static_cast<uint8_t>(bits & 0xFF));
    }
    frame.push_back(xorChecksum(frame.data() + 1, 6));
    return frame;
}

std::string buildNmea(const std::string& body) {
    uint8_t sum = xorChecksum(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    char tail[16];
    std::snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
    return "$" + body + tail;
}

Result<TemperatureMeasurement> parseTemperatureFrame(const std::vector<uint8_t>& frame, long long timestampMs) {
    typedef Result<TemperatureMeasurement> Out;
    if (frame.size() != kTemperatureFrameSize) {
        return Out::failure(ErrorCode::InvalidData, "temperature frame has wrong size");
    }
    if (frame[0] != kSyncByte) {
        return Out::failure(ErrorCode::InvalidData, "temperature frame has no sync byte");
    }
    if (static_cast<uint8_t>(frame[1] ^ frame[2]) != frame[3]) {
        return Out::failure(ErrorCode::InvalidData, "temperature checksum mismatch");
    }
    double celsius = readInt16(frame, 1) / 100.0;
    if (celsius < -40.0 || celsius > 125.0) {
        return Out::failure(ErrorCode::InvalidData, "temperature out of sensor range");
    }
    TemperatureMeasurement m;
    m.celsius = celsius;
    m.timestampMs = timestampMs;
    m.valid = true;
    return Out::success(m);
}

Result<AccelMeasurement> parseAccelFrame(const std::vector<uint8_t>& frame, int rangeG, long long timestampMs) {
    typedef Result<AccelMeasurement> Out;
    if (!isValidAccelRange(rangeG)) {
        return Out::failure(ErrorCode::InvalidArgument, "unsupported accelerometer range");
    }
    if (frame.size() != kAccelFrameSize) {
        return Out::failure(ErrorCode::InvalidData, "accelerometer frame has wrong size");
    }
    if (frame[0] != kSyncByte) {
        return Out::failure(ErrorCode::InvalidData, "accelerometer frame has no sync byte");
    }
    if (xorChecksum(frame.data() + 1, 6) != frame[7]) {
        return Out::failure(ErrorCode::InvalidData, "accelerometer checksum mismatch");
    }
    int16_t rawX = readInt16(frame, 1);
    int16_t rawY = readInt16(frame, 3);
    int16_t rawZ = readInt16(frame, 5);
    double scale = rangeG / 32768.0;

    AccelMeasurement m;
    m.x = rawX * scale;
    m.y = rawY * scale;
    m.z = rawZ * scale;
    m.rangeG = rangeG;
    m.timestampMs = timestampMs;
    bool saturated = false;
    int16_t raws[3] = {rawX, rawY, rawZ};
    for (int i = 0; i < 3; i++) {
        if (raws[i] == 32767 || raws[i] == -32768) {
            saturated = true;
        }
    }
    m.valid = !saturated;
    return Out::success(m);
}

static bool parseCoordinate(const std::string& value, const std::string& hemisphere, bool isLatitude, double& out) {
    double raw;
    if (!parseDouble(value, raw) || raw < 0) {
        return false;
    }
    double degrees = std::floor(raw / 100.0);
    double minutes = raw - degrees * 100.0;
    if (minutes >= 60.0) {
        return false;
    }
    double result = degrees + minutes / 60.0;
    double limit = isLatitude ? 90.0 : 180.0;
    if (result > limit) {
        return false;
    }
    if (isLatitude) {
        if (hemisphere == "S") {
            result = -result;
        } else if (hemisphere != "N") {
            return false;
        }
    } else {
        if (hemisphere == "W") {
            result = -result;
        } else if (hemisphere != "E") {
            return false;
        }
    }
    out = result;
    return true;
}

Result<GpsMeasurement> parseNmea(const std::string& line, long long timestampMs) {
    typedef Result<GpsMeasurement> Out;
    if (line.size() < 9 || line[0] != '$') {
        return Out::failure(ErrorCode::InvalidData, "sentence does not start with $");
    }
    size_t star = line.find('*');
    if (star == std::string::npos || star + 3 != line.size()) {
        return Out::failure(ErrorCode::InvalidData, "sentence has no valid checksum field");
    }
    std::string body = line.substr(1, star - 1);
    std::string checkText = line.substr(star + 1, 2);
    char* end = nullptr;
    long expected = std::strtol(checkText.c_str(), &end, 16);
    if (*end != '\0') {
        return Out::failure(ErrorCode::InvalidData, "checksum is not hexadecimal");
    }
    uint8_t actual = xorChecksum(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    if (actual != static_cast<uint8_t>(expected)) {
        return Out::failure(ErrorCode::InvalidData, "NMEA checksum mismatch");
    }

    std::vector<std::string> fields = split(body, ',');
    if (fields[0].size() != 5) {
        return Out::failure(ErrorCode::InvalidData, "bad sentence identifier");
    }
    std::string kind = fields[0].substr(2);

    GpsMeasurement m;
    m.timestampMs = timestampMs;

    if (kind == "GGA") {
        if (fields.size() < 10) {
            return Out::failure(ErrorCode::InvalidData, "GGA sentence is incomplete");
        }
        m.utcTime = fields[1];
        int quality = 0;
        if (!parseInt(fields[6], quality) || quality < 0) {
            return Out::failure(ErrorCode::InvalidData, "GGA fix quality is invalid");
        }
        if (quality == 0) {
            m.fixValid = false;
            return Out::success(m);
        }
        if (!parseCoordinate(fields[2], fields[3], true, m.latitude) ||
            !parseCoordinate(fields[4], fields[5], false, m.longitude)) {
            return Out::failure(ErrorCode::InvalidData, "GGA position is invalid");
        }
        m.fixValid = true;
        if (parseDouble(fields[9], m.altitude)) {
            m.hasAltitude = true;
        }
        return Out::success(m);
    }

    if (kind == "RMC") {
        if (fields.size() < 10) {
            return Out::failure(ErrorCode::InvalidData, "RMC sentence is incomplete");
        }
        m.utcTime = fields[1];
        if (fields[2] == "V") {
            m.fixValid = false;
            return Out::success(m);
        }
        if (fields[2] != "A") {
            return Out::failure(ErrorCode::InvalidData, "RMC status is invalid");
        }
        if (!parseCoordinate(fields[3], fields[4], true, m.latitude) ||
            !parseCoordinate(fields[5], fields[6], false, m.longitude)) {
            return Out::failure(ErrorCode::InvalidData, "RMC position is invalid");
        }
        m.fixValid = true;
        double knots;
        if (parseDouble(fields[7], knots)) {
            m.speedMps = knots * 0.514444;
            m.hasSpeed = true;
        }
        return Out::success(m);
    }

    return Out::failure(ErrorCode::Unsupported, "unsupported sentence " + fields[0]);
}
