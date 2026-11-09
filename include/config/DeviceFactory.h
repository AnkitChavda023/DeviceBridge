#pragma once
#include <memory>
#include "config/ConfigManager.h"
#include "core/IDevice.h"
#include "simulator/SimulatedSource.h"

struct DeviceBundle {
    std::shared_ptr<IDevice> device;
    std::shared_ptr<SimulatedSource> simulator;
};

class DeviceFactory {
public:
    static Status create(const DeviceConfig& config, DeviceBundle& bundle);
};
