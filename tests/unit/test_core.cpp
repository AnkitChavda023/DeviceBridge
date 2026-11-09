#include "TestHelper.h"
#include "core/DeviceCapability.h"
#include "core/DeviceError.h"
#include "core/DeviceInfo.h"
#include "core/DeviceState.h"
#include "core/Measurement.h"
#include "util/Logger.h"
#include "util/Util.h"
#include <cerrno>

TEST(error_names_and_status) {
    CHECK(Status::success().ok());
    Status bad = Status::failure(ErrorCode::Timeout, "slow");
    CHECK(!bad.ok());
    CHECK_EQ(bad.toString(), std::string("Timeout: slow"));
    CHECK_EQ(Status::success().toString(), std::string("Ok"));
    CHECK_EQ(std::string(errorName(ErrorCode::QueueFull)), std::string("QueueFull"));
}

TEST(result_holds_value_or_error) {
    Result<int> good = Result<int>::success(7);
    CHECK(good.ok());
    CHECK_EQ(good.value, 7);
    Result<int> bad = Result<int>::failure(ErrorCode::NotFound, "missing");
    CHECK(!bad.ok());
    CHECK_EQ(bad.status.code, ErrorCode::NotFound);
    Result<std::string> copy = Result<std::string>::fromStatus(bad.status);
    CHECK_EQ(copy.status.code, ErrorCode::NotFound);
}

TEST(errno_is_mapped_to_framework_errors) {
    CHECK_EQ(errorFromErrno(EACCES), ErrorCode::PermissionDenied);
    CHECK_EQ(errorFromErrno(EPERM), ErrorCode::PermissionDenied);
    CHECK_EQ(errorFromErrno(ENOENT), ErrorCode::Disconnected);
    CHECK_EQ(errorFromErrno(ENODEV), ErrorCode::Disconnected);
    CHECK_EQ(errorFromErrno(EAGAIN), ErrorCode::Timeout);
    CHECK_EQ(errorFromErrno(ETIMEDOUT), ErrorCode::Timeout);
    CHECK_EQ(errorFromErrno(EINVAL), ErrorCode::InvalidArgument);
    CHECK_EQ(errorFromErrno(EBUSY), ErrorCode::Busy);
    CHECK_EQ(errorFromErrno(ENOTTY), ErrorCode::Unsupported);
    CHECK_EQ(errorFromErrno(EIO), ErrorCode::IoError);
}

TEST(legal_state_transitions) {
    CHECK(canTransition(DeviceState::Discovered, DeviceState::Registered));
    CHECK(canTransition(DeviceState::Registered, DeviceState::Ready));
    CHECK(canTransition(DeviceState::Ready, DeviceState::Open));
    CHECK(canTransition(DeviceState::Open, DeviceState::Closing));
    CHECK(canTransition(DeviceState::Open, DeviceState::Error));
    CHECK(canTransition(DeviceState::Error, DeviceState::Recovering));
    CHECK(canTransition(DeviceState::Recovering, DeviceState::Ready));
    CHECK(canTransition(DeviceState::Recovering, DeviceState::Error));
    CHECK(canTransition(DeviceState::Closing, DeviceState::Closed));
    CHECK(canTransition(DeviceState::Closed, DeviceState::Open));
}

TEST(illegal_state_transitions) {
    CHECK(!canTransition(DeviceState::Discovered, DeviceState::Open));
    CHECK(!canTransition(DeviceState::Ready, DeviceState::Closed));
    CHECK(!canTransition(DeviceState::Closed, DeviceState::Error));
    CHECK(!canTransition(DeviceState::Open, DeviceState::Ready));
    CHECK(!canTransition(DeviceState::Error, DeviceState::Open));
    CHECK(!canTransition(DeviceState::Closing, DeviceState::Open));
    CHECK_EQ(std::string(stateName(DeviceState::Recovering)), std::string("Recovering"));
}

TEST(capability_helpers) {
    unsigned mask = CapMeasurement | CapConfigure;
    CHECK(hasCapability(mask, CapMeasurement));
    CHECK(hasCapability(mask, CapConfigure));
    CHECK(!hasCapability(mask, CapStreamWrite));
    CHECK_EQ(capabilityNames(mask), std::string("measurement,configure"));
    CHECK_EQ(capabilityNames(CapNone), std::string("none"));
}

TEST(device_type_parsing) {
    DeviceType type;
    CHECK(parseDeviceType("gps", type));
    CHECK_EQ(type, DeviceType::Gps);
    CHECK(parseDeviceType("temperature", type));
    CHECK(!parseDeviceType("camera", type));
    CHECK_EQ(std::string(transportTypeName(TransportType::CharDevice)), std::string("chardev"));
}

TEST(measurement_summary_and_fields) {
    TemperatureMeasurement t;
    t.celsius = 21.5;
    t.valid = true;
    Measurement m = toMeasurement("t1", t);
    CHECK_EQ(m.deviceId, std::string("t1"));
    CHECK_NEAR(m.field("temperature"), 21.5, 0.0001);
    CHECK(m.summary().find("valid") != std::string::npos);
    t.valid = false;
    CHECK(toMeasurement("t1", t).summary().find("INVALID") != std::string::npos);

    GpsMeasurement g;
    g.fixValid = true;
    g.hasAltitude = true;
    g.altitude = 10;
    CHECK_EQ(toMeasurement("g", g).fields.size(), static_cast<size_t>(3));
}

TEST(string_helpers) {
    CHECK_EQ(trim("  hi \n"), std::string("hi"));
    CHECK_EQ(toLower("AbC"), std::string("abc"));
    CHECK_EQ(split("a,,b", ',').size(), static_cast<size_t>(3));
    int number = 0;
    CHECK(parseInt("42", number));
    CHECK_EQ(number, 42);
    CHECK(!parseInt("4x", number));
    CHECK(!parseInt("", number));
    double value = 0;
    CHECK(parseDouble("2.5", value));
    CHECK(!parseDouble("2.5.1", value));
    CHECK_EQ(toHex(toBytes("AB")), std::string("41 42"));
}

TEST(logger_respects_level_and_sink) {
    std::vector<std::string> lines;
    Logger::setSink([&lines](const std::string& line) { lines.push_back(line); });
    Logger::setLevel(LogLevel::Warn);
    Logger::log(LogLevel::Info, "d1", "hidden");
    Logger::log(LogLevel::Error, "d1", "shown");
    CHECK_EQ(lines.size(), static_cast<size_t>(1));
    CHECK(lines[0].find("(d1) shown") != std::string::npos);
    Logger::setLevel(LogLevel::Off);
    Logger::log(LogLevel::Error, "d1", "silent");
    CHECK_EQ(lines.size(), static_cast<size_t>(1));
    LogLevel level;
    CHECK(Logger::parseLevel("DEBUG", level));
    CHECK(!Logger::parseLevel("loud", level));
    Logger::setSink(Logger::Sink());
    Logger::setLevel(LogLevel::Warn);
}

TEST_MAIN("unit: core types")
