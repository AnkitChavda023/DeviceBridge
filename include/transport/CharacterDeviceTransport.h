#pragma once
#include <string>
#include "transport/ITransport.h"

class CharacterDeviceTransport : public ITransport {
public:
    explicit CharacterDeviceTransport(const std::string& path, int writeTimeoutMs = 1000);
    ~CharacterDeviceTransport() override;

    Status open() override;
    void close() override;
    bool isOpen() const override;
    Result<std::vector<uint8_t> > read(size_t maxBytes, int timeoutMs) override;
    Result<size_t> write(const std::vector<uint8_t>& data) override;
    Status flushInput() override;
    bool waitReadable(int timeoutMs) override;
    TransportType type() const override { return TransportType::CharDevice; }
    std::string description() const override { return "character device " + path_; }

    Status control(unsigned long request, void* argument);
    const std::string& path() const { return path_; }

protected:
    int fd_;
    std::string path_;
    int writeTimeoutMs_;
};
