#pragma once
#include "core/Capabilities.h"
#include "devices/TransportDevice.h"

class VirtualSensorDevice : public TransportDevice,
                            public IMeasurable,
                            public IStreamReadable,
                            public IStreamWritable,
                            public IConfigurable,
                            public IPollable,
                            public IResettable {
public:
    VirtualSensorDevice(const std::string& id, const std::string& name, std::shared_ptr<ITransport> transport,
                        int timeoutMs = 500);
    ~VirtualSensorDevice() override;

    Result<Measurement> readMeasurement() override;
    Result<std::vector<uint8_t> > readBytes(size_t maxBytes, int timeoutMs) override;
    Result<size_t> writeBytes(const std::vector<uint8_t>& data) override;
    Status configure(const std::string& key, const std::string& value) override;
    std::map<std::string, std::string> currentConfig() override;
    std::vector<std::string> supportedKeys() const override;
    Result<bool> waitReadable(int timeoutMs) override;
    Status reset() override;

protected:
    Result<Measurement> sampleOnce() override;
};
