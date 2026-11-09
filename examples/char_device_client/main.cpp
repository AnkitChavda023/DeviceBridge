#include <iostream>
#include <string>
#include "ioctl_commands.h"
#include "transport/CharacterDeviceTransport.h"
#include "util/Util.h"

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "/dev/vdev0";
    CharacterDeviceTransport device(path);

    Status status = device.open();
    if (!status.ok()) {
        std::cout << "cannot open " << path << ": " << status.toString() << std::endl;
        std::cout << "load the driver first: sudo ./scripts/load_virtual_driver.sh" << std::endl;
        return 1;
    }

    struct vdev_info info;
    status = device.control(VDEV_GET_DEVICE_INFO, &info);
    if (status.ok()) {
        std::cout << "device " << info.name << " version " << info.version << " buffer " << info.buffer_size << std::endl;
    }

    __u32 interval = 100;
    status = device.control(VDEV_SET_SAMPLE_INTERVAL, &interval);
    std::cout << "set interval 100ms: " << status.toString() << std::endl;

    device.write(toBytes("hello driver\n"));

    for (int i = 0; i < 5; i++) {
        Result<std::vector<uint8_t> > data = device.read(256, 1000);
        if (data.ok()) {
            std::string text(data.value.begin(), data.value.end());
            std::cout << "read: " << text;
            if (text.empty() || text[text.size() - 1] != '\n') {
                std::cout << std::endl;
            }
        } else {
            std::cout << "read failed: " << data.status.toString() << std::endl;
        }
    }

    struct vdev_stats stats;
    status = device.control(VDEV_GET_STATS, &stats);
    if (status.ok()) {
        std::cout << "driver stats: read=" << stats.bytes_read << " written=" << stats.bytes_written
                  << " generated=" << stats.samples_generated << " dropped=" << stats.samples_dropped << std::endl;
    }

    interval = 0;
    device.control(VDEV_SET_SAMPLE_INTERVAL, &interval);
    return 0;
}
