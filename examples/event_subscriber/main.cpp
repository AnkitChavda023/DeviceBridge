#include <chrono>
#include <iostream>
#include <thread>
#include "devices/AccelerometerDevice.h"
#include "devices/TemperatureDevice.h"
#include "events/EventManager.h"
#include "manager/DeviceManager.h"
#include "simulator/Simulators.h"

int main() {
    EventManager events;
    DeviceManager manager(events);

    std::shared_ptr<VirtualTransport> tempTransport = std::make_shared<VirtualTransport>();
    std::shared_ptr<VirtualTransport> accelTransport = std::make_shared<VirtualTransport>();
    TemperatureSimulator tempSimulator(tempTransport);
    AccelerometerSimulator accelSimulator(accelTransport);

    manager.registerDevice(std::make_shared<TemperatureDevice>("temp-1", "Temperature", tempTransport));
    manager.registerDevice(std::make_shared<AccelerometerDevice>("accel-1", "Accelerometer", accelTransport));

    int subscription = events.subscribe([](const DeviceEvent& event) {
        if (event.type == EventType::MeasurementAvailable) {
            std::cout << event.deviceId << " -> " << event.measurement.summary() << std::endl;
        }
    });

    manager.openDevice("temp-1");
    manager.openDevice("accel-1");
    manager.startEvents("temp-1", 200);
    manager.startEvents("accel-1", 100);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    manager.stopEvents("temp-1");
    manager.stopEvents("accel-1");
    events.waitUntilIdle(1000);
    events.unsubscribe(subscription);
    manager.shutdown();
    return 0;
}
