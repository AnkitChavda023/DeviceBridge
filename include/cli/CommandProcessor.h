#pragma once
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "config/DeviceFactory.h"
#include "events/EventManager.h"
#include "manager/DeviceManager.h"

class CommandProcessor {
public:
    CommandProcessor(DeviceManager& manager, EventManager& events, std::ostream& out);
    ~CommandProcessor();

    Status loadConfigFile(const std::string& path);
    void run(std::istream& in);
    bool execute(const std::string& line);

private:
    void say(const std::string& text);
    void showStatus(const Status& status);
    void showHelp();
    void listDevices();
    void showInfo(const std::string& id);
    void showStatusOf(const std::string& id);
    void showStats(const std::string& id);
    void showConfig(const std::string& id);
    void readBytes(const std::string& id, int count);
    void writeText(const std::string& id, const std::string& text);
    void inject(const std::vector<std::string>& words);
    void subscribe();
    void unsubscribe();

    DeviceManager& manager_;
    EventManager& events_;
    std::ostream& out_;
    std::mutex outMutex_;
    std::map<std::string, std::shared_ptr<SimulatedSource> > simulators_;
    int subscription_;
};
