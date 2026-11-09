#include "devices/VirtualSensorDevice.h"
#include <cstdio>
#include "ioctl_commands.h"
#include "transport/CharacterDeviceTransport.h"
#include "util/Util.h"

VirtualSensorDevice::VirtualSensorDevice(const std::string& id, const std::string& name,
                                         std::shared_ptr<ITransport> transport, int timeoutMs)
    : TransportDevice(id, name, DeviceType::Virtual,
                      CapStreamRead | CapStreamWrite | CapMeasurement | CapConfigure | CapEvents | CapPollable |
                          CapReset | CapDiagnostics,
                      transport, timeoutMs) {}

VirtualSensorDevice::~VirtualSensorDevice() {
    stopEvents();
}

Result<Measurement> VirtualSensorDevice::readMeasurement() {
    return sampleOnce();
}

Result<Measurement> VirtualSensorDevice::sampleOnce() {
    typedef Result<Measurement> Out;
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return Out::fromStatus(open);
    }
    Result<std::string> line = readLine(120);
    if (!line.ok()) {
        return Out::fromStatus(handleFailure(line.status));
    }
    unsigned int sequence = 0;
    int value = 0;
    if (std::sscanf(line.value.c_str(), "SAMPLE seq=%u value=%d", &sequence, &value) != 2) {
        return Out::fromStatus(handleFailure(Status::failure(ErrorCode::InvalidData, "not a sample line: " + line.value)));
    }
    Measurement m;
    m.deviceId = info().id;
    m.type = DeviceType::Virtual;
    m.valid = true;
    m.timestampMs = nowMs();
    m.unit = "units";
    m.fields.push_back(std::make_pair("seq", static_cast<double>(sequence)));
    m.fields.push_back(std::make_pair("value", value / 1000.0));
    return Out::success(m);
}

Result<std::vector<uint8_t> > VirtualSensorDevice::readBytes(size_t maxBytes, int timeoutMs) {
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

Result<size_t> VirtualSensorDevice::writeBytes(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return Result<size_t>::fromStatus(open);
    }
    Result<size_t> result = transport_->write(data);
    if (!result.ok()) {
        return Result<size_t>::fromStatus(handleFailure(result.status));
    }
    recordWrite(result.value);
    return result;
}

Status VirtualSensorDevice::configure(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    int previous = intervalMs_;
    bool handled = false;
    Status status = configureCommon(key, value, handled);
    if (!handled) {
        return Status::failure(ErrorCode::Unsupported, "unknown setting " + key);
    }
    if (!status.ok() || key != "sample_interval_ms" || state() != DeviceState::Open) {
        return status;
    }
    CharacterDeviceTransport* chardev = dynamic_cast<CharacterDeviceTransport*>(transport_.get());
    if (chardev != nullptr) {
        __u32 interval = static_cast<__u32>(intervalMs_.load());
        status = chardev->control(VDEV_SET_SAMPLE_INTERVAL, &interval);
        if (!status.ok()) {
            intervalMs_ = previous;
        }
    }
    return status;
}

std::map<std::string, std::string> VirtualSensorDevice::currentConfig() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    return commonConfig();
}

std::vector<std::string> VirtualSensorDevice::supportedKeys() const {
    std::vector<std::string> keys;
    keys.push_back("timeout_ms");
    keys.push_back("sample_interval_ms");
    return keys;
}

Result<bool> VirtualSensorDevice::waitReadable(int timeoutMs) {
    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        Status open = requireOpen();
        if (!open.ok()) {
            return Result<bool>::fromStatus(open);
        }
        if (!rx_.empty()) {
            return Result<bool>::success(true);
        }
    }
    return Result<bool>::success(transport_->waitReadable(timeoutMs));
}

Status VirtualSensorDevice::reset() {
    std::lock_guard<std::mutex> lock(ioMutex_);
    Status open = requireOpen();
    if (!open.ok()) {
        return open;
    }
    rx_.clear();
    CharacterDeviceTransport* chardev = dynamic_cast<CharacterDeviceTransport*>(transport_.get());
    if (chardev != nullptr) {
        Status status = chardev->control(VDEV_RESET_DEVICE, nullptr);
        if (status.code != ErrorCode::Unsupported) {
            return status;
        }
    }
    return transport_->flushInput();
}
