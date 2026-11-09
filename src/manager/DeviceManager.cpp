#include "manager/DeviceManager.h"
#include <chrono>
#include <thread>
#include "util/Logger.h"

namespace {

template <typename T>
T* requireCapability(const std::shared_ptr<IDevice>& device, Capability cap, const char* label, Status& status) {
    if (!hasCapability(device->info().capabilities, cap)) {
        status = Status::failure(ErrorCode::Unsupported,
                                 "device " + device->info().id + " does not support " + label);
        return nullptr;
    }
    T* feature = dynamic_cast<T*>(device.get());
    if (feature == nullptr) {
        status = Status::failure(ErrorCode::Unsupported,
                                 "device " + device->info().id + " has no " + label + " interface");
    }
    return feature;
}

bool isPermanent(ErrorCode code) {
    return code == ErrorCode::InvalidArgument || code == ErrorCode::Unsupported ||
           code == ErrorCode::PermissionDenied;
}

}  // namespace

DeviceManager::DeviceManager(EventManager& events) : events_(events), recoveryAttempts_(3), recoveryBackoffMs_(100) {}

DeviceManager::~DeviceManager() {
    shutdown();
}

void DeviceManager::publish(EventType type, const std::string& id, const std::string& message) {
    DeviceEvent event;
    event.type = type;
    event.deviceId = id;
    event.message = message;
    events_.publish(event);
}

std::shared_ptr<IDevice> DeviceManager::lookup(const std::string& id, Status& status) const {
    std::shared_ptr<IDevice> device = registry_.find(id);
    if (!device) {
        status = Status::failure(ErrorCode::NotFound, "no device with id '" + id + "'");
    }
    return device;
}

Status DeviceManager::registerDevice(std::shared_ptr<IDevice> device) {
    if (!device) {
        return Status::failure(ErrorCode::InvalidArgument, "device is null");
    }
    if (device->info().capabilities == CapNone) {
        return Status::failure(ErrorCode::InvalidArgument, "device declares no capabilities");
    }
    Status status = registry_.add(device);
    if (!status.ok()) {
        Logger::log(LogLevel::Warn, device->info().id, "registration rejected: " + status.toString());
        return status;
    }
    device->onRegistered();
    IEventSource* source = dynamic_cast<IEventSource*>(device.get());
    if (source != nullptr) {
        EventManager* events = &events_;
        source->setEventSink([events](const DeviceEvent& event) { events->publish(event); });
    }
    Logger::log(LogLevel::Info, device->info().id, "registered");
    publish(EventType::DeviceRegistered, device->info().id, device->info().name);
    return Status::success();
}

Status DeviceManager::unregisterDevice(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    IEventSource* source = dynamic_cast<IEventSource*>(device.get());
    if (source != nullptr) {
        source->stopEvents();
    }
    DeviceState state = device->state();
    if (state == DeviceState::Open || state == DeviceState::Error) {
        device->close();
    }
    registry_.remove(id);
    if (source != nullptr) {
        source->setEventSink(EventSink());
    }
    Logger::log(LogLevel::Info, id, "unregistered");
    publish(EventType::DeviceRemoved, id, "");
    return Status::success();
}

std::vector<DeviceInfo> DeviceManager::listDevices() const {
    std::vector<DeviceInfo> result;
    std::vector<std::shared_ptr<IDevice> > devices = registry_.list();
    for (size_t i = 0; i < devices.size(); i++) {
        result.push_back(devices[i]->info());
    }
    return result;
}

std::vector<std::string> DeviceManager::findByType(DeviceType type) const {
    std::vector<std::string> ids;
    std::vector<DeviceInfo> all = listDevices();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].type == type) {
            ids.push_back(all[i].id);
        }
    }
    return ids;
}

Result<DeviceInfo> DeviceManager::getInfo(const std::string& id) const {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Result<DeviceInfo>::fromStatus(status);
    }
    return Result<DeviceInfo>::success(device->info());
}

Result<DeviceState> DeviceManager::getState(const std::string& id) const {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Result<DeviceState>::fromStatus(status);
    }
    return Result<DeviceState>::success(device->state());
}

Status DeviceManager::openDevice(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    status = device->open();
    if (status.ok()) {
        publish(EventType::DeviceOpened, id, "");
    } else {
        Logger::log(LogLevel::Warn, id, "open failed: " + status.toString());
    }
    return status;
}

Status DeviceManager::closeDevice(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    status = device->close();
    if (status.ok()) {
        publish(EventType::DeviceClosed, id, "");
    }
    return status;
}

Status DeviceManager::recoverDevice(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    for (int attempt = 1; attempt <= recoveryAttempts_; attempt++) {
        status = device->recover();
        if (status.ok()) {
            publish(EventType::DeviceRecovered, id, "after " + std::to_string(attempt) + " attempt(s)");
            return status;
        }
        if (status.code == ErrorCode::InvalidState || isPermanent(status.code)) {
            break;
        }
        if (attempt < recoveryAttempts_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(recoveryBackoffMs_ * attempt));
        }
    }
    Logger::log(LogLevel::Error, id, "recovery gave up: " + status.toString());
    return status;
}

Status DeviceManager::resetDevice(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    IResettable* feature = requireCapability<IResettable>(device, CapReset, "reset", status);
    if (feature == nullptr) {
        return status;
    }
    return feature->reset();
}

Result<Measurement> DeviceManager::readMeasurement(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Result<Measurement>::fromStatus(status);
    }
    IMeasurable* feature = requireCapability<IMeasurable>(device, CapMeasurement, "measurements", status);
    if (feature == nullptr) {
        return Result<Measurement>::fromStatus(status);
    }
    return feature->readMeasurement();
}

Result<std::vector<uint8_t> > DeviceManager::readBytes(const std::string& id, size_t maxBytes, int timeoutMs) {
    typedef Result<std::vector<uint8_t> > Out;
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Out::fromStatus(status);
    }
    IStreamReadable* feature = requireCapability<IStreamReadable>(device, CapStreamRead, "stream reads", status);
    if (feature == nullptr) {
        return Out::fromStatus(status);
    }
    return feature->readBytes(maxBytes, timeoutMs);
}

Result<size_t> DeviceManager::writeBytes(const std::string& id, const std::vector<uint8_t>& data) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Result<size_t>::fromStatus(status);
    }
    IStreamWritable* feature = requireCapability<IStreamWritable>(device, CapStreamWrite, "stream writes", status);
    if (feature == nullptr) {
        return Result<size_t>::fromStatus(status);
    }
    return feature->writeBytes(data);
}

Result<bool> DeviceManager::waitReadable(const std::string& id, int timeoutMs) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Result<bool>::fromStatus(status);
    }
    IPollable* feature = requireCapability<IPollable>(device, CapPollable, "polling", status);
    if (feature == nullptr) {
        return Result<bool>::fromStatus(status);
    }
    return feature->waitReadable(timeoutMs);
}

Status DeviceManager::configure(const std::string& id, const std::string& key, const std::string& value) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    IConfigurable* feature = requireCapability<IConfigurable>(device, CapConfigure, "configuration", status);
    if (feature == nullptr) {
        return status;
    }
    status = feature->configure(key, value);
    if (status.ok()) {
        Logger::log(LogLevel::Info, id, "configured " + key + "=" + value);
        publish(EventType::ConfigurationChanged, id, key + "=" + value);
    } else {
        Logger::log(LogLevel::Warn, id, "configure rejected: " + status.toString());
    }
    return status;
}

Result<std::map<std::string, std::string> > DeviceManager::getConfig(const std::string& id) {
    typedef Result<std::map<std::string, std::string> > Out;
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Out::fromStatus(status);
    }
    IConfigurable* feature = requireCapability<IConfigurable>(device, CapConfigure, "configuration", status);
    if (feature == nullptr) {
        return Out::fromStatus(status);
    }
    return Out::success(feature->currentConfig());
}

Result<std::vector<std::string> > DeviceManager::getSupportedKeys(const std::string& id) {
    typedef Result<std::vector<std::string> > Out;
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Out::fromStatus(status);
    }
    IConfigurable* feature = requireCapability<IConfigurable>(device, CapConfigure, "configuration", status);
    if (feature == nullptr) {
        return Out::fromStatus(status);
    }
    return Out::success(feature->supportedKeys());
}

Status DeviceManager::startEvents(const std::string& id, int intervalMs) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    IEventSource* feature = requireCapability<IEventSource>(device, CapEvents, "events", status);
    if (feature == nullptr) {
        return status;
    }
    return feature->startEvents(intervalMs);
}

Status DeviceManager::stopEvents(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return status;
    }
    IEventSource* feature = requireCapability<IEventSource>(device, CapEvents, "events", status);
    if (feature == nullptr) {
        return status;
    }
    feature->stopEvents();
    return Status::success();
}

Result<DeviceStats> DeviceManager::getStats(const std::string& id) {
    Status status;
    std::shared_ptr<IDevice> device = lookup(id, status);
    if (!device) {
        return Result<DeviceStats>::fromStatus(status);
    }
    IDiagnostics* feature = requireCapability<IDiagnostics>(device, CapDiagnostics, "diagnostics", status);
    if (feature == nullptr) {
        return Result<DeviceStats>::fromStatus(status);
    }
    return Result<DeviceStats>::success(feature->diagnostics());
}

void DeviceManager::setRecoveryPolicy(int maxAttempts, int backoffMs) {
    recoveryAttempts_ = maxAttempts < 1 ? 1 : maxAttempts;
    recoveryBackoffMs_ = backoffMs < 0 ? 0 : backoffMs;
}

void DeviceManager::shutdown() {
    std::vector<DeviceInfo> all = listDevices();
    for (size_t i = 0; i < all.size(); i++) {
        unregisterDevice(all[i].id);
    }
}
