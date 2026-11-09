#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/Capabilities.h"
#include "core/IDevice.h"
#include "events/EventManager.h"
#include "manager/DeviceRegistry.h"

class DeviceManager {
public:
    explicit DeviceManager(EventManager& events);
    ~DeviceManager();

    Status registerDevice(std::shared_ptr<IDevice> device);
    Status unregisterDevice(const std::string& id);

    std::vector<DeviceInfo> listDevices() const;
    std::vector<std::string> findByType(DeviceType type) const;
    Result<DeviceInfo> getInfo(const std::string& id) const;
    Result<DeviceState> getState(const std::string& id) const;

    Status openDevice(const std::string& id);
    Status closeDevice(const std::string& id);
    Status recoverDevice(const std::string& id);
    Status resetDevice(const std::string& id);

    Result<Measurement> readMeasurement(const std::string& id);
    Result<std::vector<uint8_t> > readBytes(const std::string& id, size_t maxBytes, int timeoutMs);
    Result<size_t> writeBytes(const std::string& id, const std::vector<uint8_t>& data);
    Result<bool> waitReadable(const std::string& id, int timeoutMs);

    Status configure(const std::string& id, const std::string& key, const std::string& value);
    Result<std::map<std::string, std::string> > getConfig(const std::string& id);
    Result<std::vector<std::string> > getSupportedKeys(const std::string& id);

    Status startEvents(const std::string& id, int intervalMs);
    Status stopEvents(const std::string& id);
    Result<DeviceStats> getStats(const std::string& id);

    void setRecoveryPolicy(int maxAttempts, int backoffMs);
    void shutdown();

private:
    std::shared_ptr<IDevice> lookup(const std::string& id, Status& status) const;
    void publish(EventType type, const std::string& id, const std::string& message);

    EventManager& events_;
    DeviceRegistry registry_;
    int recoveryAttempts_;
    int recoveryBackoffMs_;
};
