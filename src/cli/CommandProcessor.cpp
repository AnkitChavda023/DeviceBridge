#include "cli/CommandProcessor.h"
#include <chrono>
#include <sstream>
#include <thread>
#include "core/DeviceCapability.h"
#include "simulator/Simulators.h"
#include "util/Util.h"

CommandProcessor::CommandProcessor(DeviceManager& manager, EventManager& events, std::ostream& out)
    : manager_(manager), events_(events), out_(out), subscription_(0) {}

CommandProcessor::~CommandProcessor() {
    unsubscribe();
}

void CommandProcessor::say(const std::string& text) {
    std::lock_guard<std::mutex> lock(outMutex_);
    out_ << text << std::endl;
}

void CommandProcessor::showStatus(const Status& status) {
    if (status.ok()) {
        say("ok");
    } else {
        say("error: " + status.toString());
    }
}

Status CommandProcessor::loadConfigFile(const std::string& path) {
    Result<std::vector<DeviceConfig> > configs = ConfigManager::loadFile(path);
    if (!configs.ok()) {
        return configs.status;
    }
    for (size_t i = 0; i < configs.value.size(); i++) {
        DeviceBundle bundle;
        Status status = DeviceFactory::create(configs.value[i], bundle);
        if (!status.ok()) {
            return status;
        }
        status = manager_.registerDevice(bundle.device);
        if (!status.ok()) {
            return status;
        }
        if (bundle.simulator) {
            simulators_[configs.value[i].id] = bundle.simulator;
        }
    }
    return Status::success();
}

void CommandProcessor::run(std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        if (!execute(line)) {
            break;
        }
    }
}

void CommandProcessor::showHelp() {
    say("commands:");
    say("  list                         list registered devices");
    say("  info <id>                    show device details and capabilities");
    say("  open <id> | close <id>       open or close a device");
    say("  read <id>                    read one measurement");
    say("  readbytes <id> <n>           read up to n raw bytes");
    say("  write <id> <text>            write text plus newline");
    say("  set <id> <key> <value>       change a setting");
    say("  get <id>                     show current settings");
    say("  status <id>                  show state and last error");
    say("  stats <id>                   show counters");
    say("  start <id> <ms>              start event streaming");
    say("  stop <id>                    stop event streaming");
    say("  subscribe | unsubscribe      print events as they arrive");
    say("  inject <id> <what>           disconnect|reconnect|malformed <n>|delay <ms>|nofix|fix");
    say("  recover <id>                 try to recover a failed device");
    say("  reset <id>                   reset a device");
    say("  unregister <id>              remove a device");
    say("  load <file>                  load devices from a configuration file");
    say("  sleep <ms>                   wait");
    say("  quit                         leave");
}

void CommandProcessor::listDevices() {
    std::vector<DeviceInfo> all = manager_.listDevices();
    if (all.empty()) {
        say("no devices registered");
        return;
    }
    for (size_t i = 0; i < all.size(); i++) {
        Result<DeviceState> state = manager_.getState(all[i].id);
        say(all[i].id + "  type=" + deviceTypeName(all[i].type) + "  transport=" +
            transportTypeName(all[i].transport) + "  state=" + stateName(state.value));
    }
}

void CommandProcessor::showInfo(const std::string& id) {
    Result<DeviceInfo> info = manager_.getInfo(id);
    if (!info.ok()) {
        say("error: " + info.status.toString());
        return;
    }
    Result<DeviceState> state = manager_.getState(id);
    say("id: " + info.value.id);
    say("name: " + info.value.name);
    say(std::string("type: ") + deviceTypeName(info.value.type));
    say(std::string("transport: ") + transportTypeName(info.value.transport));
    say(std::string("state: ") + stateName(state.value));
    say("capabilities: " + capabilityNames(info.value.capabilities));
    Result<std::vector<std::string> > keys = manager_.getSupportedKeys(id);
    if (keys.ok()) {
        std::string joined;
        for (size_t i = 0; i < keys.value.size(); i++) {
            joined += (i > 0 ? ", " : "") + keys.value[i];
        }
        say("settings: " + joined);
    }
}

void CommandProcessor::showStatusOf(const std::string& id) {
    Result<DeviceState> state = manager_.getState(id);
    if (!state.ok()) {
        say("error: " + state.status.toString());
        return;
    }
    say(id + " state=" + stateName(state.value));
    Result<DeviceStats> stats = manager_.getStats(id);
    if (stats.ok()) {
        const DeviceStats& s = stats.value;
        say("last success: " + (s.lastSuccessMs ? formatTimestamp(s.lastSuccessMs) : std::string("never")));
        say("last error: " + (s.lastError.empty() ? std::string("none") : s.lastError + " at " + formatTimestamp(s.lastErrorMs)));
    }
}

void CommandProcessor::showStats(const std::string& id) {
    Result<DeviceStats> stats = manager_.getStats(id);
    if (!stats.ok()) {
        say("error: " + stats.status.toString());
        return;
    }
    const DeviceStats& s = stats.value;
    say("reads=" + std::to_string(s.reads) + " writes=" + std::to_string(s.writes) + " errors=" +
        std::to_string(s.errors) + " bytesRead=" + std::to_string(s.bytesRead) + " bytesWritten=" +
        std::to_string(s.bytesWritten));
}

void CommandProcessor::showConfig(const std::string& id) {
    Result<std::map<std::string, std::string> > config = manager_.getConfig(id);
    if (!config.ok()) {
        say("error: " + config.status.toString());
        return;
    }
    std::map<std::string, std::string>::const_iterator it;
    for (it = config.value.begin(); it != config.value.end(); ++it) {
        say(it->first + " = " + it->second);
    }
}

void CommandProcessor::readBytes(const std::string& id, int count) {
    if (count < 1) {
        say("error: byte count must be positive");
        return;
    }
    Result<std::vector<uint8_t> > data = manager_.readBytes(id, static_cast<size_t>(count), 500);
    if (!data.ok()) {
        say("error: " + data.status.toString());
        return;
    }
    say("read " + std::to_string(data.value.size()) + " bytes: " + toHex(data.value));
}

void CommandProcessor::writeText(const std::string& id, const std::string& text) {
    Result<size_t> written = manager_.writeBytes(id, toBytes(text + "\n"));
    if (!written.ok()) {
        say("error: " + written.status.toString());
        return;
    }
    say("wrote " + std::to_string(written.value) + " bytes");
}

void CommandProcessor::inject(const std::vector<std::string>& words) {
    if (words.size() < 3) {
        say("usage: inject <id> disconnect|reconnect|malformed <n>|delay <ms>|nofix|fix");
        return;
    }
    std::map<std::string, std::shared_ptr<SimulatedSource> >::iterator it = simulators_.find(words[1]);
    if (it == simulators_.end()) {
        say("error: no simulator for device " + words[1]);
        return;
    }
    SimulatedSource* source = it->second.get();
    const std::string& what = words[2];
    int number = 0;
    if (what == "disconnect") {
        source->disconnect();
    } else if (what == "reconnect") {
        source->reconnect();
    } else if (what == "malformed" && words.size() > 3 && parseInt(words[3], number)) {
        source->injectMalformed(number);
    } else if (what == "delay" && words.size() > 3 && parseInt(words[3], number)) {
        source->setDelayMs(number);
    } else if (what == "nofix" || what == "fix") {
        GpsSimulator* gps = dynamic_cast<GpsSimulator*>(source);
        if (gps == nullptr) {
            say("error: only GPS simulators support fix control");
            return;
        }
        gps->setFix(what == "fix");
    } else {
        say("error: unknown injection '" + what + "'");
        return;
    }
    say("ok");
}

void CommandProcessor::subscribe() {
    if (subscription_ != 0) {
        say("already subscribed");
        return;
    }
    subscription_ = events_.subscribe([this](const DeviceEvent& event) { say("[event] " + describeEvent(event)); });
    say("subscribed");
}

void CommandProcessor::unsubscribe() {
    if (subscription_ != 0) {
        events_.unsubscribe(subscription_);
        subscription_ = 0;
    }
}

bool CommandProcessor::execute(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> words;
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    if (words.empty() || words[0][0] == '#') {
        return true;
    }
    const std::string& cmd = words[0];
    std::string id = words.size() > 1 ? words[1] : "";
    int number = 0;

    if (cmd == "quit" || cmd == "exit") {
        return false;
    } else if (cmd == "help") {
        showHelp();
    } else if (cmd == "list") {
        listDevices();
    } else if (cmd == "subscribe") {
        subscribe();
    } else if (cmd == "unsubscribe") {
        unsubscribe();
        say("unsubscribed");
    } else if (cmd == "sleep" && words.size() > 1 && parseInt(words[1], number) && number >= 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(number));
    } else if (cmd == "load" && words.size() > 1) {
        showStatus(loadConfigFile(words[1]));
    } else if (cmd == "inject") {
        inject(words);
    } else if (words.size() < 2) {
        say("error: missing device id, type 'help'");
    } else if (cmd == "info") {
        showInfo(id);
    } else if (cmd == "open") {
        showStatus(manager_.openDevice(id));
    } else if (cmd == "close") {
        showStatus(manager_.closeDevice(id));
    } else if (cmd == "read") {
        Result<Measurement> m = manager_.readMeasurement(id);
        say(m.ok() ? m.value.summary() : "error: " + m.status.toString());
    } else if (cmd == "readbytes" && words.size() > 2 && parseInt(words[2], number)) {
        readBytes(id, number);
    } else if (cmd == "write" && words.size() > 2) {
        size_t start = line.find(id) + id.size();
        writeText(id, trim(line.substr(start)));
    } else if (cmd == "set" && words.size() > 3) {
        showStatus(manager_.configure(id, words[2], words[3]));
    } else if (cmd == "get") {
        showConfig(id);
    } else if (cmd == "status") {
        showStatusOf(id);
    } else if (cmd == "stats") {
        showStats(id);
    } else if (cmd == "start" && words.size() > 2 && parseInt(words[2], number)) {
        showStatus(manager_.startEvents(id, number));
    } else if (cmd == "stop") {
        showStatus(manager_.stopEvents(id));
    } else if (cmd == "recover") {
        showStatus(manager_.recoverDevice(id));
    } else if (cmd == "reset") {
        showStatus(manager_.resetDevice(id));
    } else if (cmd == "unregister") {
        showStatus(manager_.unregisterDevice(id));
    } else {
        say("error: unknown or incomplete command '" + cmd + "', type 'help'");
    }
    return true;
}
