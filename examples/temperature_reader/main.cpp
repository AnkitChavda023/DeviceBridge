#include <iostream>
#include "devices/TemperatureDevice.h"
#include "events/EventManager.h"
#include "manager/DeviceManager.h"
#include "simulator/Simulators.h"
#include "transport/VirtualTransport.h"

int main() {
    EventManager events;
    DeviceManager manager(events);

    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>();
    TemperatureSimulator simulator(transport);
    simulator.setFixedValue(23.75);

    std::shared_ptr<TemperatureDevice> sensor =
        std::make_shared<TemperatureDevice>("temp-example", "Example Temperature Sensor", transport);

    Status status = manager.registerDevice(sensor);
    if (!status.ok()) {
        std::cout << "register failed: " << status.toString() << std::endl;
        return 1;
    }
    status = manager.openDevice("temp-example");
    if (!status.ok()) {
        std::cout << "open failed: " << status.toString() << std::endl;
        return 1;
    }

    for (int i = 0; i < 3; i++) {
        Result<Measurement> reading = manager.readMeasurement("temp-example");
        if (reading.ok()) {
            std::cout << "reading " << i + 1 << ": " << reading.value.summary() << std::endl;
        } else {
            std::cout << "read failed: " << reading.status.toString() << std::endl;
        }
    }

    manager.closeDevice("temp-example");
    manager.shutdown();
    return 0;
}
