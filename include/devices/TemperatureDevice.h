#pragma once
#include "core/Capabilities.h"
#include "devices/ITemperatureSensor.h"
#include "devices/TransportDevice.h"

class TemperatureDevice : public TransportDevice,
                          public ITemperatureSensor,
                          public IMeasurable,
                          public IConfigurable,
                          public IResettable {
public:
    TemperatureDevice(const std::string& id, const std::string& name, std::shared_ptr<ITransport> transport,
                      int timeoutMs = 500);
    ~TemperatureDevice() override;

    Result<TemperatureMeasurement> readTemperature() override;
    Result<Measurement> readMeasurement() override;
    Status configure(const std::string& key, const std::string& value) override;
    std::map<std::string, std::string> currentConfig() override;
    std::vector<std::string> supportedKeys() const override;
    Status reset() override;

protected:
    Result<Measurement> sampleOnce() override;
};
