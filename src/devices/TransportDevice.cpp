#include "devices/TransportDevice.h"
#include "devices/Parsers.h"
#include "util/Util.h"

static DeviceInfo makeInfo(const std::string& id, const std::string& name, DeviceType type, unsigned capabilities,
                           const std::shared_ptr<ITransport>& transport) {
    DeviceInfo info;
    info.id = id;
    info.name = name;
    info.type = type;
    info.capabilities = capabilities;
    info.transport = transport->type();
    return info;
}

TransportDevice::TransportDevice(const std::string& id, const std::string& name, DeviceType type,
                                 unsigned capabilities, std::shared_ptr<ITransport> transport, int timeoutMs)
    : DeviceBase(makeInfo(id, name, type, capabilities, transport)),
      transport_(transport),
      timeoutMs_(timeoutMs),
      discardingLine_(false) {}

Status TransportDevice::doOpen() {
    Status status = transport_->open();
    if (!status.ok()) {
        return status;
    }
    transport_->flushInput();
    rx_.clear();
    discardingLine_ = false;
    return Status::success();
}

void TransportDevice::doClose() {
    transport_->close();
    rx_.clear();
    discardingLine_ = false;
}

Status TransportDevice::doRecover() {
    transport_->close();
    rx_.clear();
    discardingLine_ = false;
    Status status = transport_->open();
    if (!status.ok()) {
        return status;
    }
    transport_->close();
    return Status::success();
}

Result<std::vector<uint8_t> > TransportDevice::readFrame(size_t frameLength) {
    typedef Result<std::vector<uint8_t> > Out;
    long long deadline = steadyMs() + timeoutMs_;
    while (true) {
        while (!rx_.empty() && rx_.front() != kSyncByte) {
            rx_.erase(rx_.begin());
        }
        if (rx_.size() >= frameLength) {
            std::vector<uint8_t> frame(rx_.begin(), rx_.begin() + frameLength);
            rx_.erase(rx_.begin(), rx_.begin() + frameLength);
            recordRead(frameLength);
            return Out::success(frame);
        }
        long long left = deadline - steadyMs();
        if (left <= 0) {
            return Out::failure(ErrorCode::Timeout, "no complete frame before timeout");
        }
        Result<std::vector<uint8_t> > chunk = transport_->read(64, static_cast<int>(left));
        if (!chunk.ok()) {
            return chunk;
        }
        rx_.insert(rx_.end(), chunk.value.begin(), chunk.value.end());
    }
}

static bool findNewline(const std::vector<uint8_t>& data, size_t& position) {
    for (size_t i = 0; i < data.size(); i++) {
        if (data[i] == '\n') {
            position = i;
            return true;
        }
    }
    return false;
}

Result<std::string> TransportDevice::readLine(size_t maxLength) {
    typedef Result<std::string> Out;
    long long deadline = steadyMs() + timeoutMs_;
    while (true) {
        size_t newline = 0;
        if (discardingLine_) {
            if (findNewline(rx_, newline)) {
                rx_.erase(rx_.begin(), rx_.begin() + newline + 1);
                discardingLine_ = false;
            } else {
                rx_.clear();
            }
        }
        bool found = findNewline(rx_, newline);
        if (found) {
            std::string line(rx_.begin(), rx_.begin() + newline);
            rx_.erase(rx_.begin(), rx_.begin() + newline + 1);
            if (!line.empty() && line[line.size() - 1] == '\r') {
                line.erase(line.size() - 1);
            }
            recordRead(newline + 1);
            if (line.size() > maxLength) {
                return Out::failure(ErrorCode::InvalidData, "line is too long");
            }
            return Out::success(line);
        }
        if (rx_.size() > maxLength) {
            rx_.clear();
            discardingLine_ = true;
            return Out::failure(ErrorCode::InvalidData, "no line terminator within maximum length");
        }
        long long left = deadline - steadyMs();
        if (left <= 0) {
            return Out::failure(ErrorCode::Timeout, "no complete line before timeout");
        }
        Result<std::vector<uint8_t> > chunk = transport_->read(64, static_cast<int>(left));
        if (!chunk.ok()) {
            return Out::fromStatus(chunk.status);
        }
        rx_.insert(rx_.end(), chunk.value.begin(), chunk.value.end());
    }
}

Status TransportDevice::sendBytes(const std::vector<uint8_t>& data) {
    Result<size_t> result = transport_->write(data);
    if (!result.ok()) {
        return result.status;
    }
    recordWrite(result.value);
    return Status::success();
}

Status TransportDevice::configureCommon(const std::string& key, const std::string& value, bool& handled) {
    handled = true;
    int number = 0;
    if (key == "timeout_ms") {
        if (!parseInt(value, number) || number < 10 || number > 10000) {
            return Status::failure(ErrorCode::InvalidArgument, "timeout_ms must be 10..10000");
        }
        timeoutMs_ = number;
        return Status::success();
    }
    if (key == "sample_interval_ms") {
        if (!parseInt(value, number) || number < 10 || number > 60000) {
            return Status::failure(ErrorCode::InvalidArgument, "sample_interval_ms must be 10..60000");
        }
        intervalMs_ = number;
        return Status::success();
    }
    handled = false;
    return Status::success();
}

std::map<std::string, std::string> TransportDevice::commonConfig() const {
    std::map<std::string, std::string> config;
    config["timeout_ms"] = std::to_string(timeoutMs_);
    config["sample_interval_ms"] = std::to_string(intervalMs_.load());
    return config;
}
