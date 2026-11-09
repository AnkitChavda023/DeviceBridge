#include <fstream>
#include <iostream>
#include <string>
#include "cli/CommandProcessor.h"
#include "util/Logger.h"

int main(int argc, char** argv) {
    std::string configPath = "config/virtual_devices.yaml";
    std::string scriptPath;
    Logger::setLevel(LogLevel::Warn);

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--script" && i + 1 < argc) {
            scriptPath = argv[++i];
        } else if (arg == "--log" && i + 1 < argc) {
            LogLevel level;
            if (!Logger::parseLevel(argv[++i], level)) {
                std::cerr << "unknown log level" << std::endl;
                return 2;
            }
            Logger::setLevel(level);
        } else {
            std::cerr << "usage: device_monitor [--config file] [--script file] [--log debug|info|warn|error|off]" << std::endl;
            return 2;
        }
    }

    EventManager events(256);
    DeviceManager manager(events);
    CommandProcessor processor(manager, events, std::cout);

    Status loaded = processor.loadConfigFile(configPath);
    if (!loaded.ok()) {
        std::cerr << "configuration error: " << loaded.toString() << std::endl;
        return 1;
    }
    std::cout << "device_monitor ready, type 'help' for commands" << std::endl;

    if (!scriptPath.empty()) {
        std::ifstream script(scriptPath.c_str());
        if (!script) {
            std::cerr << "cannot open script " << scriptPath << std::endl;
            return 1;
        }
        processor.run(script);
    } else {
        processor.run(std::cin);
    }

    manager.shutdown();
    return 0;
}
