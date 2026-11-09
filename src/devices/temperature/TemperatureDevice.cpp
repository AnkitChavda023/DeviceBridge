#include "devices/TemperatureDevice.h"
#include "devices/Parsers.h"
#include "util/Util.h"

TemperatureDevice::TemperatureDevice(const std::string& id, const std::string& name,
                                     std::shared_ptr<ITransport> transport, int timeoutMs)
    : TransportDevice(id, name, DeviceType::Temperature,
                      CapMeasurement | CapConfigure | CapEvents | CapReset | CapDiagnostics, transport, timeoutMs) {}

TemperatureDevice::~TemperatureDevice() {
    stopEvents();
}

Result<TemperatureMeasurement> TemperatureDevice::readTemperature() {
    typedef Result<TemperatureMeasurement> Out;
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return Out::fromStatus(open);
    }
    Result<std::vector<uint8_t> > frame = readFrame(kTemperatureFrameSize);
    if (!frame.ok()) {
        return Out::fromStatus(handleFailure(frame.status));
    }
    Out parsed = parseTemperatureFrame(frame.value, nowMs());
    if (!parsed.ok()) {
        return Out::fromStatus(handleFailure(parsed.status));
    }
    return parsed;
}

Result<Measurement> TemperatureDevice::readMeasurement() {
    return sampleOnce();
}

Result<Measurement> TemperatureDevice::sampleOnce() {
    Result<TemperatureMeasurement> reading = readTemperature();
    if (!reading.ok()) {
        return Result<Measurement>::fromStatus(reading.status);
    }
    return Result<Measurement>::success(toMeasurement(info().id, reading.value));
}

Status TemperatureDevice::configure(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    bool handled = false;
    Status status = configureCommon(key, value, handled);
    if (handled) {
        return status;
    }
    return Status::failure(ErrorCode::Unsupported, "unknown setting " + key);
}

std::map<std::string, std::string> TemperatureDevice::currentConfig() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    return commonConfig();
}

std::vector<std::string> TemperatureDevice::supportedKeys() const {
    std::vector<std::string> keys;
    keys.push_back("timeout_ms");
    keys.push_back("sample_interval_ms");
    return keys;
}

Status TemperatureDevice::reset() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return open;
    }
    rx_.clear();
    return transport_->flushInput();
}
