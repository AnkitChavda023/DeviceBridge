#include <chrono>
#include <thread>
#include "Rigs.h"
#include "TestHelper.h"
#include "devices/Parsers.h"

TEST(new_device_is_discovered_then_ready_when_registered) {
    TempRig rig;
    CHECK_EQ(rig.device->state(), DeviceState::Discovered);
    rig.device->onRegistered();
    CHECK_EQ(rig.device->state(), DeviceState::Ready);
}

TEST(device_info_describes_capabilities) {
    TempRig temp;
    CHECK(hasCapability(temp.device->info().capabilities, CapMeasurement));
    CHECK(hasCapability(temp.device->info().capabilities, CapEvents));
    CHECK(!hasCapability(temp.device->info().capabilities, CapStreamWrite));
    CHECK_EQ(temp.device->info().transport, TransportType::Virtual);
    CHECK_EQ(temp.device->info().type, DeviceType::Temperature);

    GpsRig gps;
    CHECK(hasCapability(gps.device->info().capabilities, CapStreamRead));
    VdevRig vdev;
    CHECK(hasCapability(vdev.device->info().capabilities, CapStreamWrite));
    CHECK(hasCapability(vdev.device->info().capabilities, CapPollable));
    AccelRig accel;
    CHECK(!hasCapability(accel.device->info().capabilities, CapStreamRead));
}

TEST(open_and_close_cycle_can_repeat) {
    TempRig rig;
    rig.device->onRegistered();
    for (int i = 0; i < 3; i++) {
        CHECK_OK(rig.device->open());
        CHECK_EQ(rig.device->state(), DeviceState::Open);
        CHECK_OK(rig.device->close());
        CHECK_EQ(rig.device->state(), DeviceState::Closed);
    }
}

TEST(open_before_registration_is_rejected) {
    TempRig rig;
    CHECK_CODE(rig.device->open(), ErrorCode::InvalidState);
}

TEST(open_twice_and_close_twice_are_rejected) {
    TempRig rig;
    rig.device->onRegistered();
    CHECK_CODE(rig.device->close(), ErrorCode::InvalidState);
    CHECK_OK(rig.device->open());
    CHECK_CODE(rig.device->open(), ErrorCode::InvalidState);
    CHECK_OK(rig.device->close());
    CHECK_CODE(rig.device->close(), ErrorCode::InvalidState);
}

TEST(reading_requires_open_state) {
    TempRig rig;
    rig.device->onRegistered();
    CHECK_CODE(rig.device->readTemperature().status, ErrorCode::InvalidState);
    rig.device->open();
    CHECK(rig.device->readTemperature().ok());
    rig.device->close();
    CHECK_CODE(rig.device->readTemperature().status, ErrorCode::InvalidState);
    CHECK_CODE(rig.device->startEvents(100), ErrorCode::InvalidState);
}

TEST(recover_only_works_from_error_state) {
    TempRig rig;
    rig.device->onRegistered();
    CHECK_CODE(rig.device->recover(), ErrorCode::InvalidState);
    rig.device->open();
    CHECK_CODE(rig.device->recover(), ErrorCode::InvalidState);
}

TEST(stale_bytes_are_dropped_when_device_opens) {
    TempRig rig;
    rig.device->onRegistered();
    rig.simulator->setFixedValue(30.0);
    rig.transport->open();
    rig.simulator->generateOne();
    rig.transport->close();
    rig.simulator->setFixedValue(10.0);
    rig.device->open();
    Result<TemperatureMeasurement> m = rig.device->readTemperature();
    CHECK(m.ok());
    CHECK_NEAR(m.value.celsius, 10.0, 0.01);
}

TEST(garbage_before_a_frame_is_skipped) {
    TempRig rig;
    rig.device->onRegistered();
    rig.device->open();
    std::vector<uint8_t> noise;
    noise.push_back(0x01);
    noise.push_back(0x02);
    noise.push_back(0x03);
    rig.transport->feed(noise);
    rig.transport->feed(buildTemperatureFrame(18.5));
    Result<TemperatureMeasurement> m = rig.device->readTemperature();
    CHECK(m.ok());
    CHECK_NEAR(m.value.celsius, 18.5, 0.01);
}

TEST(frame_split_across_reads_is_reassembled) {
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>();
    TemperatureDevice device("t", "bare", transport, 500);
    device.onRegistered();
    device.open();
    std::vector<uint8_t> frame = buildTemperatureFrame(22.0);
    std::vector<uint8_t> firstHalf(frame.begin(), frame.begin() + 2);
    std::vector<uint8_t> secondHalf(frame.begin() + 2, frame.end());
    transport->feed(firstHalf);
    std::thread late([transport, secondHalf] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        transport->feed(secondHalf);
    });
    Result<TemperatureMeasurement> m = device.readTemperature();
    late.join();
    CHECK(m.ok());
    CHECK_NEAR(m.value.celsius, 22.0, 0.01);
}

TEST(counters_track_reads_and_errors) {
    TempRig rig;
    rig.device->onRegistered();
    rig.device->open();
    rig.device->readTemperature();
    rig.device->readTemperature();
    DeviceStats stats = rig.device->diagnostics();
    CHECK_EQ(stats.reads, 2ULL);
    CHECK_EQ(stats.bytesRead, 8ULL);
    CHECK_EQ(stats.errors, 0ULL);
    CHECK(stats.lastSuccessMs > 0);

    rig.simulator->injectMalformed(1);
    CHECK(!rig.device->readTemperature().ok());
    stats = rig.device->diagnostics();
    CHECK_EQ(stats.errors, 1ULL);
    CHECK(stats.lastError.find("InvalidData") != std::string::npos);
    CHECK(stats.lastErrorMs > 0);
}

TEST_MAIN("unit: device lifecycle")
