#include "transport/VirtualTransport.h"
#include <chrono>
#include <thread>

VirtualTransport::VirtualTransport(size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity),
      open_(false),
      connected_(true),
      readDelayMs_(0),
      failWrites_(0),
      chunkSize_(0),
      overflow_(0),
      chunked_(0) {}

Status VirtualTransport::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return Status::failure(ErrorCode::Disconnected, "virtual device is not connected");
    }
    open_ = true;
    return Status::success();
}

void VirtualTransport::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }
    cv_.notify_all();
}

bool VirtualTransport::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

Result<std::vector<uint8_t> > VirtualTransport::read(size_t maxBytes, int timeoutMs) {
    typedef Result<std::vector<uint8_t> > Out;
    if (maxBytes == 0 || maxBytes > kMaxTransferSize) {
        return Out::failure(ErrorCode::InvalidArgument, "read size out of range");
    }

    int delay;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) {
            return Out::failure(ErrorCode::InvalidState, "transport is closed");
        }
        if (!connected_) {
            return Out::failure(ErrorCode::Disconnected, "virtual device disconnected");
        }
        delay = readDelayMs_;
    }

    if (delay > 0) {
        if (delay > timeoutMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
            return Out::failure(ErrorCode::Timeout, "read timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        timeoutMs -= delay;
    }

    bool empty;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        empty = rx_.empty();
    }
    if (empty) {
        std::function<void()> provider;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            provider = provider_;
            if (provider) {
                provider();
            }
        }
    }

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                 [this] { return !rx_.empty() || !connected_ || !open_; });
    if (!open_) {
        return Out::failure(ErrorCode::InvalidState, "transport closed while reading");
    }
    if (!connected_) {
        return Out::failure(ErrorCode::Disconnected, "virtual device disconnected");
    }
    if (rx_.empty()) {
        return Out::failure(ErrorCode::Timeout, "read timed out");
    }
    std::vector<uint8_t> data;
    while (!rx_.empty() && data.size() < maxBytes) {
        data.push_back(rx_.front());
        rx_.pop_front();
    }
    return Out::success(data);
}

Result<size_t> VirtualTransport::write(const std::vector<uint8_t>& data) {
    if (data.empty() || data.size() > kMaxTransferSize) {
        return Result<size_t>::failure(ErrorCode::InvalidArgument, "write size out of range");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) {
            return Result<size_t>::failure(ErrorCode::InvalidState, "transport is closed");
        }
        if (!connected_) {
            return Result<size_t>::failure(ErrorCode::Disconnected, "virtual device disconnected");
        }
        if (failWrites_ > 0) {
            failWrites_--;
            return Result<size_t>::failure(ErrorCode::IoError, "injected write failure");
        }
        size_t step = chunkSize_ == 0 ? data.size() : chunkSize_;
        size_t sent = 0;
        while (sent < data.size()) {
            size_t count = data.size() - sent;
            if (count > step) {
                count = step;
            }
            written_.insert(written_.end(), data.begin() + sent, data.begin() + sent + count);
            sent += count;
            if (sent < data.size()) {
                chunked_++;
            }
        }
        if (written_.size() > kMaxTransferSize) {
            written_.erase(written_.begin(), written_.end() - kMaxTransferSize);
        }
    }

    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (writeHandler_) {
        writeHandler_(data);
    }
    return Result<size_t>::success(data.size());
}

Status VirtualTransport::flushInput() {
    std::lock_guard<std::mutex> lock(mutex_);
    rx_.clear();
    return Status::success();
}

bool VirtualTransport::waitReadable(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                        [this] { return !rx_.empty() || !connected_ || !open_; }) &&
           !rx_.empty();
}

void VirtualTransport::feed(const std::vector<uint8_t>& data) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < data.size(); i++) {
            if (rx_.size() >= capacity_) {
                rx_.pop_front();
                overflow_++;
            }
            rx_.push_back(data[i]);
        }
    }
    cv_.notify_all();
}

std::vector<uint8_t> VirtualTransport::takeWritten() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint8_t> result = written_;
    written_.clear();
    return result;
}

void VirtualTransport::setConnected(bool connected) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = connected;
    }
    cv_.notify_all();
}

bool VirtualTransport::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

void VirtualTransport::setReadDelayMs(int ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    readDelayMs_ = ms;
}

void VirtualTransport::setFailNextWrites(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    failWrites_ = count;
}

void VirtualTransport::setWriteChunkSize(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    chunkSize_ = bytes;
}

void VirtualTransport::setDataProvider(std::function<void()> provider) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    provider_ = provider;
}

void VirtualTransport::setWriteHandler(std::function<void(const std::vector<uint8_t>&)> handler) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    writeHandler_ = handler;
}

size_t VirtualTransport::pendingBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rx_.size();
}

unsigned long long VirtualTransport::overflowBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return overflow_;
}

unsigned long long VirtualTransport::chunkedWrites() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunked_;
}
