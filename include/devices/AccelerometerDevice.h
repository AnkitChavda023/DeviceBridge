#pragma once
#include "core/Capabilities.h"
#include "devices/IAccelerometer.h"
#include "devices/TransportDevice.h"

const uint8_t kAccelSetRangeCommand = 0xC1;

class AccelerometerDevice : public TransportDevice,
                            public IAccelerometer,
                            public IMeasurable,
                            public IConfigurable,
                            public IResettable {
public:
    AccelerometerDevice(const std::string& id, const std::string& name, std::shared_ptr<ITransport> transport,
                        int rangeG = 2, int timeoutMs = 500);
    ~AccelerometerDevice() override;

    Result<AccelMeasurement> readAcceleration() override;
    Result<Measurement> readMeasurement() override;
    Status configure(const std::string& key, const std::string& value) override;
    std::map<std::string, std::string> currentConfig() override;
    std::vector<std::string> supportedKeys() const override;
    Status reset() override;

protected:
    Status doOpen() override;
    Result<Measurement> sampleOnce() override;

private:
    Status sendRange();

    int rangeG_;
};
