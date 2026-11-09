#pragma once
#include "core/Capabilities.h"
#include "devices/IGpsDevice.h"
#include "devices/TransportDevice.h"

const size_t kMaxNmeaLength = 120;

class GpsDevice : public TransportDevice,
                  public IGpsDevice,
                  public IMeasurable,
                  public IStreamReadable,
                  public IConfigurable,
                  public IResettable {
public:
    GpsDevice(const std::string& id, const std::string& name, std::shared_ptr<ITransport> transport,
              int timeoutMs = 1000);
    ~GpsDevice() override;

    Result<GpsMeasurement> readPosition() override;
    Result<Measurement> readMeasurement() override;
    Result<std::vector<uint8_t> > readBytes(size_t maxBytes, int timeoutMs) override;
    Status configure(const std::string& key, const std::string& value) override;
    std::map<std::string, std::string> currentConfig() override;
    std::vector<std::string> supportedKeys() const override;
    Status reset() override;

protected:
    Result<Measurement> sampleOnce() override;
};
