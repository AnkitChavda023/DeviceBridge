#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/DeviceError.h"
#include "core/Measurement.h"

const uint8_t kSyncByte = 0xA5;
const size_t kTemperatureFrameSize = 4;
const size_t kAccelFrameSize = 8;

uint8_t xorChecksum(const uint8_t* data, size_t count);
bool isValidAccelRange(int rangeG);

std::vector<uint8_t> buildTemperatureFrame(double celsius);
std::vector<uint8_t> buildAccelFrame(double x, double y, double z, int rangeG);
std::string buildNmea(const std::string& body);

Result<TemperatureMeasurement> parseTemperatureFrame(const std::vector<uint8_t>& frame, long long timestampMs);
Result<AccelMeasurement> parseAccelFrame(const std::vector<uint8_t>& frame, int rangeG, long long timestampMs);
Result<GpsMeasurement> parseNmea(const std::string& line, long long timestampMs);
