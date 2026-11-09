#include "devices/GpsDevice.h"
#include "devices/Parsers.h"
#include "util/Util.h"

GpsDevice::GpsDevice(const std::string& id, const std::string& name, std::shared_ptr<ITransport> transport,
                     int timeoutMs)
    : TransportDevice(id, name, DeviceType::Gps,
                      CapStreamRead | CapMeasurement | CapConfigure | CapEvents | CapReset | CapDiagnostics,
                      transport, timeoutMs) {}

GpsDevice::~GpsDevice() {
    stopEvents();
}

Result<GpsMeasurement> GpsDevice::readPosition() {
    typedef Result<GpsMeasurement> Out;
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return Out::fromStatus(open);
    }
    for (int attempt = 0; attempt < 10; attempt++) {
        Result<std::string> line = readLine(kMaxNmeaLength);
        if (!line.ok()) {
            return Out::fromStatus(handleFailure(line.status));
        }
        Out parsed = parseNmea(line.value, nowMs());
        if (parsed.status.code == ErrorCode::Unsupported) {
            continue;
        }
        if (!parsed.ok()) {
            return Out::fromStatus(handleFailure(parsed.status));
        }
        return parsed;
    }
    return Out::failure(ErrorCode::Timeout, "no supported sentence received");
}

Result<Measurement> GpsDevice::readMeasurement() {
    return sampleOnce();
}

Result<Measurement> GpsDevice::sampleOnce() {
    Result<GpsMeasurement> reading = readPosition();
    if (!reading.ok()) {
        return Result<Measurement>::fromStatus(reading.status);
    }
    return Result<Measurement>::success(toMeasurement(info().id, reading.value));
}

Result<std::vector<uint8_t> > GpsDevice::readBytes(size_t maxBytes, int timeoutMs) {
    typedef Result<std::vector<uint8_t> > Out;
    if (maxBytes == 0 || maxBytes > kMaxTransferSize) {
        return Out::failure(ErrorCode::InvalidArgument, "maxBytes must be 1..4096");
    }
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return Out::fromStatus(open);
    }
    if (!rx_.empty()) {
        size_t count = rx_.size() < maxBytes ? rx_.size() : maxBytes;
        std::vector<uint8_t> data(rx_.begin(), rx_.begin() + count);
        rx_.erase(rx_.begin(), rx_.begin() + count);
        recordRead(count);
        return Out::success(data);
    }
    Out result = transport_->read(maxBytes, timeoutMs);
    if (!result.ok()) {
        return Out::fromStatus(handleFailure(result.status));
    }
    recordRead(result.value.size());
    return result;
}

Status GpsDevice::configure(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    bool handled = false;
    Status status = configureCommon(key, value, handled);
    if (handled) {
        return status;
    }
    return Status::failure(ErrorCode::Unsupported, "unknown setting " + key);
}

std::map<std::string, std::string> GpsDevice::currentConfig() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    return commonConfig();
}

std::vector<std::string> GpsDevice::supportedKeys() const {
    std::vector<std::string> keys;
    keys.push_back("timeout_ms");
    keys.push_back("sample_interval_ms");
    return keys;
}

Status GpsDevice::reset() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return open;
    }
    rx_.clear();
    return transport_->flushInput();
}
