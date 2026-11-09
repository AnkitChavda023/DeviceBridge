#include "events/DeviceEvent.h"
#include "util/Util.h"

const char* eventTypeName(EventType type) {
    switch (type) {
        case EventType::DeviceRegistered: return "DeviceRegistered";
        case EventType::DeviceRemoved: return "DeviceRemoved";
        case EventType::DeviceOpened: return "DeviceOpened";
        case EventType::DeviceClosed: return "DeviceClosed";
        case EventType::MeasurementAvailable: return "MeasurementAvailable";
        case EventType::DeviceError: return "DeviceError";
        case EventType::DeviceDisconnected: return "DeviceDisconnected";
        case EventType::DeviceRecovered: return "DeviceRecovered";
        case EventType::ConfigurationChanged: return "ConfigurationChanged";
    }
    return "Unknown";
}

std::string describeEvent(const DeviceEvent& event) {
    std::string text = formatTimestamp(event.timestampMs) + " #" + std::to_string(event.sequence) +
                       " " + eventTypeName(event.type) + " device=" + event.deviceId;
    if (event.hasMeasurement) {
        text += " " + event.measurement.summary();
    } else if (!event.message.empty()) {
        text += " " + event.message;
    }
    return text;
}
