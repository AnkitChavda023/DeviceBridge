#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include "transport/ITransport.h"

class VirtualTransport : public ITransport {
public:
    explicit VirtualTransport(size_t capacity = 256);

    Status open() override;
    void close() override;
    bool isOpen() const override;
    Result<std::vector<uint8_t> > read(size_t maxBytes, int timeoutMs) override;
    Result<size_t> write(const std::vector<uint8_t>& data) override;
    Status flushInput() override;
    bool waitReadable(int timeoutMs) override;
    TransportType type() const override { return TransportType::Virtual; }
    std::string description() const override { return "virtual in-process transport"; }

    void feed(const std::vector<uint8_t>& data);
    std::vector<uint8_t> takeWritten();
    void setConnected(bool connected);
    bool isConnected() const;
    void setReadDelayMs(int ms);
    void setFailNextWrites(int count);
    void setWriteChunkSize(size_t bytes);
    void setDataProvider(std::function<void()> provider);
    void setWriteHandler(std::function<void(const std::vector<uint8_t>&)> handler);

    size_t pendingBytes() const;
    unsigned long long overflowBytes() const;
    unsigned long long chunkedWrites() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<uint8_t> rx_;
    std::vector<uint8_t> written_;
    size_t capacity_;
    bool open_;
    bool connected_;
    int readDelayMs_;
    int failWrites_;
    size_t chunkSize_;
    unsigned long long overflow_;
    unsigned long long chunked_;

    std::mutex callbackMutex_;
    std::function<void()> provider_;
    std::function<void(const std::vector<uint8_t>&)> writeHandler_;
};
