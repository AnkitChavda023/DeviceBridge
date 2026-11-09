#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "core/IDevice.h"

class DeviceRegistry {
public:
    static bool isValidId(const std::string& id);

    Status add(std::shared_ptr<IDevice> device);
    std::shared_ptr<IDevice> find(const std::string& id) const;
    bool remove(const std::string& id);
    std::vector<std::shared_ptr<IDevice> > list() const;
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<IDevice> > devices_;
};
