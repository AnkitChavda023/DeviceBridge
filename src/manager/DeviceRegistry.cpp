#include "manager/DeviceRegistry.h"

bool DeviceRegistry::isValidId(const std::string& id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (size_t i = 0; i < id.size(); i++) {
        char c = id[i];
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

Status DeviceRegistry::add(std::shared_ptr<IDevice> device) {
    if (!device) {
        return Status::failure(ErrorCode::InvalidArgument, "device is null");
    }
    const std::string& id = device->info().id;
    if (!isValidId(id)) {
        return Status::failure(ErrorCode::InvalidArgument, "invalid device id '" + id + "'");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (devices_.count(id) > 0) {
        return Status::failure(ErrorCode::AlreadyExists, "device id '" + id + "' is already registered");
    }
    devices_[id] = device;
    return Status::success();
}

std::shared_ptr<IDevice> DeviceRegistry::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, std::shared_ptr<IDevice> >::const_iterator it = devices_.find(id);
    if (it == devices_.end()) {
        return std::shared_ptr<IDevice>();
    }
    return it->second;
}

bool DeviceRegistry::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_.erase(id) > 0;
}

std::vector<std::shared_ptr<IDevice> > DeviceRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<IDevice> > result;
    std::map<std::string, std::shared_ptr<IDevice> >::const_iterator it;
    for (it = devices_.begin(); it != devices_.end(); ++it) {
        result.push_back(it->second);
    }
    return result;
}

size_t DeviceRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_.size();
}
