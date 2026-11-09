#pragma once
#include "transport/CharacterDeviceTransport.h"

class TtyTransport : public CharacterDeviceTransport {
public:
    TtyTransport(const std::string& path, int baud);

    Status open() override;
    TransportType type() const override { return TransportType::UART; }
    std::string description() const override { return "tty " + path_; }

    static bool isSupportedBaud(int baud);

private:
    int baud_;
};
