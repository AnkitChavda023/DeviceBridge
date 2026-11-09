#include "simulator/Simulators.h"
#include <cmath>
#include <cstdio>
#include "devices/AccelerometerDevice.h"
#include "devices/Parsers.h"
#include "util/Util.h"

TemperatureSimulator::TemperatureSimulator(std::shared_ptr<VirtualTransport> transport)
    : SimulatedSource(transport), step_(0), fixed_(false), fixedValue_(25.0) {}

TemperatureSimulator::~TemperatureSimulator() {
    detach();
}

void TemperatureSimulator::setFixedValue(double celsius) {
    std::lock_guard<std::mutex> lock(mutex_);
    fixed_ = true;
    fixedValue_ = celsius;
}

void TemperatureSimulator::useWave() {
    std::lock_guard<std::mutex> lock(mutex_);
    fixed_ = false;
}

std::vector<uint8_t> TemperatureSimulator::produce() {
    double celsius = fixedValue_;
    if (!fixed_) {
        celsius = 25.0 + 3.0 * std::sin(step_ * 0.3);
    }
    step_++;
    return buildTemperatureFrame(celsius);
}

AccelerometerSimulator::AccelerometerSimulator(std::shared_ptr<VirtualTransport> transport)
    : SimulatedSource(transport), step_(0), fixed_(false), fixedX_(0), fixedY_(0), fixedZ_(1), rangeG_(2) {}

AccelerometerSimulator::~AccelerometerSimulator() {
    detach();
}

void AccelerometerSimulator::setFixedValue(double x, double y, double z) {
    std::lock_guard<std::mutex> lock(mutex_);
    fixed_ = true;
    fixedX_ = x;
    fixedY_ = y;
    fixedZ_ = z;
}

void AccelerometerSimulator::useMotion() {
    std::lock_guard<std::mutex> lock(mutex_);
    fixed_ = false;
}

int AccelerometerSimulator::range() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rangeG_;
}

std::vector<uint8_t> AccelerometerSimulator::produce() {
    double x = fixedX_;
    double y = fixedY_;
    double z = fixedZ_;
    if (!fixed_) {
        x = 0.25 * std::sin(step_ * 0.5);
        y = 0.25 * std::cos(step_ * 0.5);
        z = 1.0;
    }
    step_++;
    return buildAccelFrame(x, y, z, rangeG_);
}

void AccelerometerSimulator::handleWrite(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i + 1 < data.size(); i++) {
        if (data[i] == kAccelSetRangeCommand && isValidAccelRange(data[i + 1])) {
            rangeG_ = data[i + 1];
            i++;
        }
    }
}

GpsSimulator::GpsSimulator(std::shared_ptr<VirtualTransport> transport)
    : SimulatedSource(transport), step_(0), hasFix_(true), latitude_(12.9716), longitude_(77.5946) {}

GpsSimulator::~GpsSimulator() {
    detach();
}

void GpsSimulator::setFix(bool hasFix) {
    std::lock_guard<std::mutex> lock(mutex_);
    hasFix_ = hasFix;
}

static std::string formatCoordinate(double degrees, bool isLatitude) {
    double value = std::fabs(degrees);
    int whole = static_cast<int>(value);
    double minutes = (value - whole) * 60.0;
    char buffer[32];
    if (isLatitude) {
        std::snprintf(buffer, sizeof(buffer), "%02d%07.4f", whole, minutes);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%03d%07.4f", whole, minutes);
    }
    return buffer;
}

std::vector<uint8_t> GpsSimulator::produce() {
    int seconds = static_cast<int>(step_ / 2) % 60;
    char clock[16];
    std::snprintf(clock, sizeof(clock), "1235%02d", seconds);

    std::string body;
    char text[160];
    if (step_ % 2 == 0) {
        if (hasFix_) {
            std::snprintf(text, sizeof(text), "GPGGA,%s,%s,N,%s,E,1,08,0.9,%.1f,M,46.9,M,,", clock,
                          formatCoordinate(latitude_, true).c_str(), formatCoordinate(longitude_, false).c_str(),
                          920.0 + (step_ % 10));
        } else {
            std::snprintf(text, sizeof(text), "GPGGA,%s,,,,,0,00,99.9,,M,,M,,", clock);
        }
    } else {
        if (hasFix_) {
            std::snprintf(text, sizeof(text), "GPRMC,%s,A,%s,N,%s,E,%.1f,84.4,230394,,,A", clock,
                          formatCoordinate(latitude_, true).c_str(), formatCoordinate(longitude_, false).c_str(),
                          5.0 + (step_ % 5));
            latitude_ += 0.0001;
            longitude_ += 0.0001;
        } else {
            std::snprintf(text, sizeof(text), "GPRMC,%s,V,,,,,,,230394,,,N", clock);
        }
    }
    step_++;
    body = text;
    return toBytes(buildNmea(body));
}

std::vector<uint8_t> GpsSimulator::corrupt(const std::vector<uint8_t>& frame, int variant) {
    std::string line(frame.begin(), frame.end());
    if (variant % 2 == 0) {
        size_t star = line.find('*');
        if (star != std::string::npos && star + 1 < line.size()) {
            line[star + 1] = (line[star + 1] == '0') ? '1' : '0';
        }
    } else {
        line = line.substr(0, line.size() / 2) + "\r\n";
    }
    return toBytes(line);
}

VirtualDeviceSimulator::VirtualDeviceSimulator(std::shared_ptr<VirtualTransport> transport)
    : SimulatedSource(transport), sequence_(0) {}

VirtualDeviceSimulator::~VirtualDeviceSimulator() {
    detach();
}

std::vector<uint8_t> VirtualDeviceSimulator::produce() {
    sequence_++;
    char text[64];
    std::snprintf(text, sizeof(text), "SAMPLE seq=%u value=%d\n", sequence_,
                  20000 + static_cast<int>(sequence_ % 100) * 50);
    return toBytes(text);
}

std::vector<uint8_t> VirtualDeviceSimulator::corrupt(const std::vector<uint8_t>&, int) {
    return toBytes("SAMPLE garbage\n");
}

void VirtualDeviceSimulator::handleWrite(const std::vector<uint8_t>& data) {
    transport_->feed(data);
}
