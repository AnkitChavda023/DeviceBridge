#pragma once
#include <memory>
#include <string>
#include "devices/AccelerometerDevice.h"
#include "devices/GpsDevice.h"
#include "devices/TemperatureDevice.h"
#include "devices/VirtualSensorDevice.h"
#include "events/EventManager.h"
#include "manager/DeviceManager.h"
#include "simulator/Simulators.h"
#include "util/Util.h"

struct TempRig {
    std::shared_ptr<VirtualTransport> transport;
    std::shared_ptr<TemperatureSimulator> simulator;
    std::shared_ptr<TemperatureDevice> device;

    explicit TempRig(const std::string& id = "temp-1", int timeoutMs = 200) {
        transport = std::make_shared<VirtualTransport>(256);
        simulator = std::make_shared<TemperatureSimulator>(transport);
        simulator->setFixedValue(25.0);
        device = std::make_shared<TemperatureDevice>(id, "Test Temperature", transport, timeoutMs);
    }
};

struct AccelRig {
    std::shared_ptr<VirtualTransport> transport;
    std::shared_ptr<AccelerometerSimulator> simulator;
    std::shared_ptr<AccelerometerDevice> device;

    explicit AccelRig(const std::string& id = "accel-1", int timeoutMs = 200) {
        transport = std::make_shared<VirtualTransport>(256);
        simulator = std::make_shared<AccelerometerSimulator>(transport);
        simulator->setFixedValue(0.5, -0.25, 1.0);
        device = std::make_shared<AccelerometerDevice>(id, "Test Accelerometer", transport, 2, timeoutMs);
    }
};

struct GpsRig {
    std::shared_ptr<VirtualTransport> transport;
    std::shared_ptr<GpsSimulator> simulator;
    std::shared_ptr<GpsDevice> device;

    explicit GpsRig(const std::string& id = "gps-1", int timeoutMs = 300) {
        transport = std::make_shared<VirtualTransport>(512);
        simulator = std::make_shared<GpsSimulator>(transport);
        device = std::make_shared<GpsDevice>(id, "Test GPS", transport, timeoutMs);
    }
};

struct VdevRig {
    std::shared_ptr<VirtualTransport> transport;
    std::shared_ptr<VirtualDeviceSimulator> simulator;
    std::shared_ptr<VirtualSensorDevice> device;

    explicit VdevRig(const std::string& id = "vdev-1", int timeoutMs = 200) {
        transport = std::make_shared<VirtualTransport>(512);
        simulator = std::make_shared<VirtualDeviceSimulator>(transport);
        device = std::make_shared<VirtualSensorDevice>(id, "Test Virtual Device", transport, timeoutMs);
    }
};
