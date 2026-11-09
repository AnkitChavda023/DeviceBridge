#include "TestHelper.h"
#include "config/ConfigManager.h"

TEST(shipped_configuration_file_loads) {
    Result<std::vector<DeviceConfig> > result = ConfigManager::loadFile("config/virtual_devices.yaml");
    CHECK_OK(result.status);
    CHECK_EQ(result.value.size(), static_cast<size_t>(4));
    CHECK_EQ(result.value[0].id, std::string("temp-virtual-01"));
    CHECK_EQ(result.value[1].rangeG, 2);
    CHECK_EQ(result.value[2].timeoutMs, 1000);
}

TEST(missing_file_is_reported) {
    CHECK_CODE(ConfigManager::loadFile("config/does_not_exist.yaml").status, ErrorCode::NotFound);
}

TEST(comments_quotes_and_defaults) {
    std::string text =
        "# comment\n"
        "devices:\n"
        "  - id: t1   # trailing comment\n"
        "    type: temperature\n"
        "    name: \"Quoted Name\"\n";
    Result<std::vector<DeviceConfig> > result = ConfigManager::parseText(text);
    CHECK_OK(result.status);
    CHECK_EQ(result.value.size(), static_cast<size_t>(1));
    CHECK_EQ(result.value[0].name, std::string("Quoted Name"));
    CHECK_EQ(result.value[0].transport, std::string("virtual"));
    CHECK_EQ(result.value[0].sampleIntervalMs, 200);
}

TEST(missing_name_defaults_to_id) {
    Result<std::vector<DeviceConfig> > result = ConfigManager::parseText("- id: gps1\n  type: gps\n");
    CHECK_OK(result.status);
    CHECK_EQ(result.value[0].name, std::string("gps1"));
}

TEST(bad_values_are_rejected) {
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: camera\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- type: gps\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a b\n  type: gps\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  transport: usb\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  transport: chardev\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  sample_interval_ms: 5\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  timeout_ms: abc\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: accelerometer\n  range_g: 3\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  transport: tty\n  path: /dev/x\n  baud: 1234\n").status,
               ErrorCode::InvalidArgument);
}

TEST(unknown_keys_and_garbage_are_rejected) {
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  colour: red\n").status, ErrorCode::Unsupported);
    CHECK_CODE(ConfigManager::parseText("id: a\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type gps\n").status, ErrorCode::InvalidArgument);
}

TEST(buffer_capacity_is_configurable_and_validated) {
    Result<std::vector<DeviceConfig> > ok = ConfigManager::parseText("- id: a\n  type: gps\n  buffer_capacity: 1024\n");
    CHECK_OK(ok.status);
    CHECK_EQ(ok.value[0].bufferCapacity, 1024);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  buffer_capacity: 4\n").status, ErrorCode::InvalidArgument);
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n  buffer_capacity: 99999\n").status, ErrorCode::InvalidArgument);
}

TEST(duplicate_ids_are_rejected) {
    CHECK_CODE(ConfigManager::parseText("- id: a\n  type: gps\n- id: a\n  type: temperature\n").status,
               ErrorCode::AlreadyExists);
}

TEST(chardev_and_tty_entries_are_accepted) {
    std::string text =
        "- id: c1\n  type: virtual\n  transport: chardev\n  path: /dev/vdev0\n"
        "- id: u1\n  type: gps\n  transport: tty\n  path: /dev/ttyUSB0\n  baud: 115200\n";
    Result<std::vector<DeviceConfig> > result = ConfigManager::parseText(text);
    CHECK_OK(result.status);
    CHECK_EQ(result.value[1].baud, 115200);
}

TEST_MAIN("unit: configuration")
