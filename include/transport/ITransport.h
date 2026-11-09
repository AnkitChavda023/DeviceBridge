#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/DeviceError.h"
#include "core/DeviceInfo.h"

const size_t kMaxTransferSize = 4096;

class ITransport {
public:
    virtual ~ITransport() {}
    virtual Status open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual Result<std::vector<uint8_t> > read(size_t maxBytes, int timeoutMs) = 0;
    virtual Result<size_t> write(const std::vector<uint8_t>& data) = 0;
    virtual Status flushInput() = 0;
    virtual bool waitReadable(int timeoutMs) = 0;
    virtual TransportType type() const = 0;
    virtual std::string description() const = 0;
};
