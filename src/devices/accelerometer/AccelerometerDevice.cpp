#include "devices/AccelerometerDevice.h"
#include "devices/Parsers.h"
#include "util/Util.h"

AccelerometerDevice::AccelerometerDevice(const std::string& id, const std::string& name,
                                         std::shared_ptr<ITransport> transport, int rangeG, int timeoutMs)
    : TransportDevice(id, name, DeviceType::Accelerometer,
                      CapMeasurement | CapConfigure | CapEvents | CapReset | CapDiagnostics, transport, timeoutMs),
      rangeG_(rangeG) {}

AccelerometerDevice::~AccelerometerDevice() {
    stopEvents();
}

Status AccelerometerDevice::sendRange() {
    std::vector<uint8_t> command;
    command.push_back(kAccelSetRangeCommand);
    command.push_back(static_cast<uint8_t>(rangeG_));
    Status status = sendBytes(command);
    if (!status.ok()) {
        return status;
    }
    rx_.clear();
    return transport_->flushInput();
}

Status AccelerometerDevice::doOpen() {
    Status status = TransportDevice::doOpen();
    if (!status.ok()) {
        return status;
    }
    status = sendRange();
    if (!status.ok()) {
        transport_->close();
    }
    return status;
}

Result<AccelMeasurement> AccelerometerDevice::readAcceleration() {
    typedef Result<AccelMeasurement> Out;
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return Out::fromStatus(open);
    }
    Result<std::vector<uint8_t> > frame = readFrame(kAccelFrameSize);
    if (!frame.ok()) {
        return Out::fromStatus(handleFailure(frame.status));
    }
    Out parsed = parseAccelFrame(frame.value, rangeG_, nowMs());
    if (!parsed.ok()) {
        return Out::fromStatus(handleFailure(parsed.status));
    }
    return parsed;
}

Result<Measurement> AccelerometerDevice::readMeasurement() {
    return sampleOnce();
}

Result<Measurement> AccelerometerDevice::sampleOnce() {
    Result<AccelMeasurement> reading = readAcceleration();
    if (!reading.ok()) {
        return Result<Measurement>::fromStatus(reading.status);
    }
    return Result<Measurement>::success(toMeasurement(info().id, reading.value));
}

Status AccelerometerDevice::configure(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    if (key == "range_g") {
        int range = 0;
        if (!parseInt(value, range) || !isValidAccelRange(range)) {
            return Status::failure(ErrorCode::InvalidArgument, "range_g must be 2, 4, 8 or 16");
        }
        int previous = rangeG_;
        rangeG_ = range;
        if (state() == DeviceState::Open) {
            Status status = sendRange();
            if (!status.ok()) {
                rangeG_ = previous;
                return handleFailure(status);
            }
        }
        return Status::success();
    }
    bool handled = false;
    Status status = configureCommon(key, value, handled);
    if (handled) {
        return status;
    }
    return Status::failure(ErrorCode::Unsupported, "unknown setting " + key);
}

std::map<std::string, std::string> AccelerometerDevice::currentConfig() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    std::map<std::string, std::string> config = commonConfig();
    config["range_g"] = std::to_string(rangeG_);
    return config;
}

std::vector<std::string> AccelerometerDevice::supportedKeys() const {
    std::vector<std::string> keys;
    keys.push_back("range_g");
    keys.push_back("timeout_ms");
    keys.push_back("sample_interval_ms");
    return keys;
}

Status AccelerometerDevice::reset() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return open;
    }
    return sendRange();
}
