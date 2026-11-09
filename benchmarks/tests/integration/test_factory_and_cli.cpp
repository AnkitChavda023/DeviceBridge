#include <sstream>
#include "TestHelper.h"
#include "cli/CommandProcessor.h"
#include "config/DeviceFactory.h"

static bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

static std::string runScript(const std::string& script, bool loadDefaults = true) {
    EventManager events;
    DeviceManager manager(events);
    std::ostringstream out;
    {
        CommandProcessor processor(manager, events, out);
        if (loadDefaults) {
            Status status = processor.loadConfigFile("config/virtual_devices.yaml");
            if (!status.ok()) {
                out << "LOAD FAILED " << status.toString() << "\n";
            }
        }
        std::istringstream in(script);
        processor.run(in);
        events.waitUntilIdle(1000);
    }
    manager.shutdown();
    return out.str();
}

TEST(factory_builds_every_device_type) {
    const char* types[] = {"temperature", "accelerometer", "gps", "virtual"};
    for (int i = 0; i < 4; i++) {
        DeviceConfig config;
        config.id = std::string("dev-") + types[i];
        config.type = types[i];
        DeviceBundle bundle;
        CHECK_OK(DeviceFactory::create(config, bundle));
        CHECK(bundle.device != nullptr);
        CHECK(bundle.simulator != nullptr);
        CHECK_EQ(bundle.device->info().id, config.id);
        CHECK_EQ(bundle.device->info().transport, TransportType::Virtual);
    }
}

TEST(factory_applies_configuration) {
    DeviceConfig config;
    config.id = "a1";
    config.type = "accelerometer";
    config.rangeG = 8;
    config.sampleIntervalMs = 50;
    DeviceBundle bundle;
    CHECK_OK(DeviceFactory::create(config, bundle));
    IConfigurable* configurable = dynamic_cast<IConfigurable*>(bundle.device.get());
    CHECK(configurable != nullptr);
    CHECK_EQ(configurable->currentConfig()["range_g"], std::string("8"));
    CHECK_EQ(configurable->currentConfig()["sample_interval_ms"], std::string("50"));
}

TEST(factory_rejects_invalid_configuration) {
    DeviceConfig config;
    config.id = "bad id";
    config.type = "gps";
    DeviceBundle bundle;
    CHECK_CODE(DeviceFactory::create(config, bundle), ErrorCode::InvalidArgument);
    CHECK(bundle.device == nullptr);
}

TEST(factory_chardev_without_node_fails_at_open) {
    DeviceConfig config;
    config.id = "c1";
    config.type = "virtual";
    config.transport = "chardev";
    config.path = "/tmp/fw_missing_node";
    DeviceBundle bundle;
    CHECK_OK(DeviceFactory::create(config, bundle));
    CHECK(bundle.simulator == nullptr);
    CHECK_EQ(bundle.device->info().transport, TransportType::CharDevice);
    bundle.device->onRegistered();
    CHECK_CODE(bundle.device->open(), ErrorCode::Disconnected);
    CHECK_EQ(bundle.device->state(), DeviceState::Ready);
}

TEST(factory_tty_device_uses_uart_transport) {
    DeviceConfig config;
    config.id = "u1";
    config.type = "gps";
    config.transport = "tty";
    config.path = "/tmp/fw_missing_tty";
    DeviceBundle bundle;
    CHECK_OK(DeviceFactory::create(config, bundle));
    CHECK_EQ(bundle.device->info().transport, TransportType::UART);
}

TEST(cli_lists_and_describes_devices) {
    std::string out = runScript("list\ninfo temp-virtual-01\ninfo gps-virtual-01\n");
    CHECK(contains(out, "temp-virtual-01  type=temperature  transport=virtual  state=Ready"));
    CHECK(contains(out, "accel-virtual-01"));
    CHECK(contains(out, "gps-virtual-01"));
    CHECK(contains(out, "capabilities: measurement,configure,events,reset,diagnostics"));
    CHECK(contains(out, "settings: timeout_ms, sample_interval_ms"));
}

TEST(cli_reads_every_device) {
    std::string out = runScript(
        "open temp-virtual-01\nread temp-virtual-01\n"
        "open accel-virtual-01\nread accel-virtual-01\n"
        "open gps-virtual-01\nread gps-virtual-01\n"
        "open test-virtual-01\nread test-virtual-01\n");
    CHECK(contains(out, "temperature: temperature="));
    CHECK(contains(out, "accelerometer: x="));
    CHECK(contains(out, "gps: latitude="));
    CHECK(contains(out, "virtual: seq="));
    CHECK(!contains(out, "error"));
}

TEST(cli_reports_errors_clearly) {
    std::string out = runScript(
        "read temp-virtual-01\nopen nothing\nset temp-virtual-01 timeout_ms 1\nfrobnicate x\nreadbytes temp-virtual-01 4\nopen temp-virtual-01\n"
        "readbytes temp-virtual-01 4\n");
    CHECK(contains(out, "error: InvalidState"));
    CHECK(contains(out, "error: NotFound"));
    CHECK(contains(out, "error: InvalidArgument"));
    CHECK(contains(out, "unknown or incomplete command"));
    CHECK(contains(out, "error: Unsupported"));
}

TEST(cli_configure_status_and_stats) {
    std::string out = runScript(
        "set accel-virtual-01 range_g 8\nget accel-virtual-01\nopen test-virtual-01\nwrite test-virtual-01 hello world\n"
        "readbytes test-virtual-01 11\nstats test-virtual-01\nstatus test-virtual-01\n");
    CHECK(contains(out, "range_g = 8"));
    CHECK(contains(out, "wrote 12 bytes"));
    CHECK(contains(out, "writes=1"));
    CHECK(contains(out, "bytesWritten=12"));
    CHECK(contains(out, "state=Open"));
    CHECK(contains(out, "last error: none"));
}

TEST(cli_failure_injection_and_recovery) {
    std::string out = runScript(
        "open temp-virtual-01\nread temp-virtual-01\n"
        "inject temp-virtual-01 disconnect\nread temp-virtual-01\nstatus temp-virtual-01\n"
        "recover temp-virtual-01\ninject temp-virtual-01 reconnect\nrecover temp-virtual-01\n"
        "open temp-virtual-01\nread temp-virtual-01\n");
    CHECK(contains(out, "error: Disconnected"));
    CHECK(contains(out, "state=Error"));
    CHECK(contains(out, "temperature: temperature="));
}

TEST(cli_malformed_data_injection) {
    std::string out = runScript(
        "open gps-virtual-01\ninject gps-virtual-01 malformed 2\nread gps-virtual-01\nread gps-virtual-01\nread gps-virtual-01\n");
    CHECK(contains(out, "error: InvalidData"));
    CHECK(contains(out, "gps: latitude="));
}

TEST(cli_gps_fix_control) {
    std::string out = runScript("open gps-virtual-01\ninject gps-virtual-01 nofix\nread gps-virtual-01\ninject temp-virtual-01 nofix\n");
    CHECK(contains(out, "INVALID"));
    CHECK(contains(out, "only GPS simulators support fix control"));
}

TEST(cli_event_subscription_and_streaming) {
    std::string out = runScript(
        "subscribe\nopen temp-virtual-01\nstart temp-virtual-01 20\nsleep 250\nstop temp-virtual-01\nclose temp-virtual-01\nsleep 150\nunsubscribe\n");
    CHECK(contains(out, "subscribed"));
    CHECK(contains(out, "[event]"));
    CHECK(contains(out, "MeasurementAvailable device=temp-virtual-01"));
    CHECK(contains(out, "DeviceOpened device=temp-virtual-01"));
    CHECK(contains(out, "DeviceClosed device=temp-virtual-01"));
}

TEST(cli_unregister_and_reload) {
    std::string out = runScript("unregister temp-virtual-01\nlist\nload config/virtual_devices.yaml\nload nothing.yaml\n");
    CHECK(contains(out, "error: AlreadyExists"));
    CHECK(contains(out, "error: NotFound"));
    CHECK(!contains(out, "LOAD FAILED"));
}

TEST(cli_help_and_empty_lines) {
    std::string out = runScript("\n# a comment\nhelp\nsleep 1\nlist\nquit\nlist\n", false);
    CHECK(contains(out, "commands:"));
    CHECK(contains(out, "no devices registered"));
    size_t first = out.find("no devices registered");
    CHECK(out.find("no devices registered", first + 1) == std::string::npos);
}

TEST_MAIN("integration: factory and CLI")
