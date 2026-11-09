#pragma once
#include <string>
#include "core/Measurement.h"

enum class EventType {
    DeviceRegistered,
    DeviceRemoved,
    DeviceOpened,
    DeviceClosed,
    MeasurementAvailable,
    DeviceError,
    DeviceDisconnected,
    DeviceRecovered,
    ConfigurationChanged
};

struct DeviceEvent {
    EventType type = EventType::DeviceError;
    std::string deviceId;
    long long timestampMs = 0;
    unsigned long long sequence = 0;
    std::string message;
    bool hasMeasurement = false;
    Measurement measurement;
};

const char* eventTypeName(EventType type);
std::string describeEvent(const DeviceEvent& event);
