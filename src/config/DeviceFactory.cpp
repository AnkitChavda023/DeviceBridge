#include "config/DeviceFactory.h"
#include "devices/AccelerometerDevice.h"
#include "devices/GpsDevice.h"
#include "devices/TemperatureDevice.h"
#include "devices/VirtualSensorDevice.h"
#include "simulator/Simulators.h"
#include "transport/CharacterDeviceTransport.h"
#include "transport/TtyTransport.h"

Status DeviceFactory::create(const DeviceConfig& config, DeviceBundle& bundle) {
    Status valid = ConfigManager::validate(config);
    if (!valid.ok()) {
        return valid;
    }
    DeviceType type;
    parseDeviceType(config.type, type);
    std::string name = config.name.empty() ? config.id : config.name;

    std::shared_ptr<ITransport> transport;
    std::shared_ptr<VirtualTransport> virtualTransport;
    if (config.transport == "virtual") {
        virtualTransport = std::make_shared<VirtualTransport>(static_cast<size_t>(config.bufferCapacity));
        transport = virtualTransport;
    } else if (config.transport == "chardev") {
        transport = std::make_shared<CharacterDeviceTransport>(config.path);
    } else {
        transport = std::make_shared<TtyTransport>(config.path, config.baud);
    }

    std::shared_ptr<DeviceBase> device;
    std::shared_ptr<SimulatedSource> simulator;
    if (type == DeviceType::Temperature) {
        device = std::make_shared<TemperatureDevice>(config.id, name, transport, config.timeoutMs);
        if (virtualTransport) {
            simulator = std::make_shared<TemperatureSimulator>(virtualTransport);
        }
    } else if (type == DeviceType::Accelerometer) {
        device = std::make_shared<AccelerometerDevice>(config.id, name, transport, config.rangeG, config.timeoutMs);
        if (virtualTransport) {
            simulator = std::make_shared<AccelerometerSimulator>(virtualTransport);
        }
    } else if (type == DeviceType::Gps) {
        device = std::make_shared<GpsDevice>(config.id, name, transport, config.timeoutMs);
        if (virtualTransport) {
            simulator = std::make_shared<GpsSimulator>(virtualTransport);
        }
    } else {
        device = std::make_shared<VirtualSensorDevice>(config.id, name, transport, config.timeoutMs);
        if (virtualTransport) {
            simulator = std::make_shared<VirtualDeviceSimulator>(virtualTransport);
        }
    }

    IConfigurable* configurable = dynamic_cast<IConfigurable*>(device.get());
    if (configurable != nullptr) {
        configurable->configure("sample_interval_ms", std::to_string(config.sampleIntervalMs));
    }

    bundle.device = device;
    bundle.simulator = simulator;
    return Status::success();
}
