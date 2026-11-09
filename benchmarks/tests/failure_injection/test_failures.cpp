#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "Rigs.h"
#include "TestHelper.h"
#include "devices/Parsers.h"

struct Watcher {
    std::mutex mutex;
    std::vector<DeviceEvent> events;
    EventManager* source = nullptr;
    int subscription = 0;

    ~Watcher() {
        if (source != nullptr) {
            source->unsubscribe(subscription);
        }
    }
    void attach(EventManager& manager) {
        source = &manager;
        subscription = manager.subscribe([this](const DeviceEvent& e) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back(e);
        });
    }
    int count(EventType type) {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (size_t i = 0; i < events.size(); i++) {
            if (events[i].type == type) n++;
        }
        return n;
    }
};

static void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

TEST(unavailable_device_cannot_be_opened_until_it_returns) {
    EventManager events;
    DeviceManager manager(events);
    TempRig rig;
    manager.registerDevice(rig.device);
    rig.simulator->disconnect();
    CHECK_CODE(manager.openDevice("temp-1"), ErrorCode::Disconnected);
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Ready);
    CHECK_EQ(manager.getStats("temp-1").value.errors, 1ULL);
    rig.simulator->reconnect();
    CHECK_OK(manager.openDevice("temp-1"));
    CHECK(manager.readMeasurement("temp-1").ok());
}

TEST(disconnect_during_operation_moves_device_to_error) {
    EventManager events;
    DeviceManager manager(events);
    Watcher watcher;
    watcher.attach(events);
    TempRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    CHECK(manager.readMeasurement("temp-1").ok());

    rig.simulator->disconnect();
    CHECK_CODE(manager.readMeasurement("temp-1").status, ErrorCode::Disconnected);
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Error);
    CHECK_CODE(manager.readMeasurement("temp-1").status, ErrorCode::InvalidState);

    events.waitUntilIdle(2000);
    CHECK_EQ(watcher.count(EventType::DeviceError), 1);
    CHECK_EQ(watcher.count(EventType::DeviceDisconnected), 1);
    CHECK(manager.getStats("temp-1").value.lastError.find("Disconnected") != std::string::npos);
}

TEST(read_timeout_keeps_the_device_open) {
    EventManager events;
    DeviceManager manager(events);
    TempRig rig("temp-1", 100);
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    rig.simulator->setDelayMs(1000);
    CHECK_CODE(manager.readMeasurement("temp-1").status, ErrorCode::Timeout);
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Open);
    CHECK_EQ(manager.getStats("temp-1").value.errors, 1ULL);
    rig.simulator->setDelayMs(0);
    CHECK(manager.readMeasurement("temp-1").ok());
}

TEST(delay_injection_slows_reads_without_failing_them) {
    TempRig rig("temp-1", 500);
    rig.device->onRegistered();
    rig.device->open();
    rig.simulator->setDelayMs(120);
    long long start = steadyMs();
    CHECK(rig.device->readTemperature().ok());
    CHECK(steadyMs() - start >= 110);
}

TEST(write_failure_puts_device_in_error_state) {
    EventManager events;
    DeviceManager manager(events);
    Watcher watcher;
    watcher.attach(events);
    VdevRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("vdev-1");
    rig.transport->setFailNextWrites(1);
    CHECK_CODE(manager.writeBytes("vdev-1", toBytes("data")).status, ErrorCode::IoError);
    CHECK_EQ(manager.getState("vdev-1").value, DeviceState::Error);
    events.waitUntilIdle(2000);
    CHECK_EQ(watcher.count(EventType::DeviceError), 1);
    CHECK_EQ(watcher.count(EventType::DeviceDisconnected), 0);
}

TEST(partial_transfers_are_completed) {
    EventManager events;
    DeviceManager manager(events);
    VdevRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("vdev-1");
    rig.transport->setWriteChunkSize(3);
    Result<size_t> written = manager.writeBytes("vdev-1", toBytes("0123456789"));
    CHECK(written.ok());
    CHECK_EQ(written.value, static_cast<size_t>(10));
    CHECK(rig.transport->chunkedWrites() > 0);
    CHECK_EQ(manager.getStats("vdev-1").value.bytesWritten, 10ULL);
}

TEST(failed_range_command_leaves_old_range_in_place) {
    AccelRig rig;
    rig.device->onRegistered();
    rig.device->open();
    rig.transport->setFailNextWrites(1);
    CHECK_CODE(rig.device->configure("range_g", "8"), ErrorCode::IoError);
    CHECK_EQ(rig.device->currentConfig()["range_g"], std::string("2"));
    CHECK_EQ(rig.device->state(), DeviceState::Error);
}

TEST(malformed_temperature_frames_are_rejected_then_stream_recovers) {
    TempRig rig;
    rig.device->onRegistered();
    rig.device->open();
    rig.simulator->injectMalformed(2);
    CHECK_CODE(rig.device->readTemperature().status, ErrorCode::InvalidData);
    CHECK_CODE(rig.device->readTemperature().status, ErrorCode::InvalidData);
    CHECK_EQ(rig.device->state(), DeviceState::Open);
    CHECK(rig.device->readTemperature().ok());
    CHECK_EQ(rig.device->diagnostics().errors, 2ULL);
}

TEST(malformed_accelerometer_frames_are_rejected) {
    AccelRig rig;
    rig.device->onRegistered();
    rig.device->open();
    rig.simulator->injectMalformed(1);
    CHECK_CODE(rig.device->readAcceleration().status, ErrorCode::InvalidData);
    CHECK(rig.device->readAcceleration().ok());
}

TEST(malformed_gps_sentences_are_rejected_in_both_forms) {
    GpsRig rig;
    rig.device->onRegistered();
    rig.device->open();
    rig.simulator->injectMalformed(2);
    CHECK_CODE(rig.device->readPosition().status, ErrorCode::InvalidData);
    CHECK_CODE(rig.device->readPosition().status, ErrorCode::InvalidData);
    CHECK(rig.device->readPosition().ok());
    CHECK_EQ(rig.device->state(), DeviceState::Open);
}

TEST(incomplete_gps_sentence_times_out_then_completes) {
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>();
    GpsDevice gps("gps", "bare", transport, 100);
    gps.onRegistered();
    gps.open();
    std::string line = buildNmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    transport->feed(toBytes(line.substr(0, 20)));
    CHECK_CODE(gps.readPosition().status, ErrorCode::Timeout);
    transport->feed(toBytes(line.substr(20)));
    Result<GpsMeasurement> m = gps.readPosition();
    CHECK(m.ok());
    CHECK_NEAR(m.value.latitude, 48.1173, 0.0001);
}

TEST(overlong_gps_line_is_discarded) {
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>(1024);
    GpsDevice gps("gps", "bare", transport, 100);
    gps.onRegistered();
    gps.open();
    transport->feed(toBytes(std::string(300, 'x') + "\r\n"));
    CHECK_CODE(gps.readPosition().status, ErrorCode::InvalidData);
    transport->feed(toBytes(buildNmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,")));
    CHECK(gps.readPosition().ok());
}

TEST(unsupported_gps_sentences_are_skipped) {
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>(1024);
    GpsDevice gps("gps", "bare", transport, 200);
    gps.onRegistered();
    gps.open();
    transport->feed(toBytes(buildNmea("GPGSV,3,1,11,03,03,111,00")));
    transport->feed(toBytes(buildNmea("GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1")));
    transport->feed(toBytes(buildNmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,")));
    CHECK(gps.readPosition().ok());
}

TEST(full_event_queue_drops_events_but_devices_keep_working) {
    EventManager events(3);
    DeviceManager manager(events);
    std::atomic<bool> release(false);
    events.subscribe([&](const DeviceEvent&) {
        while (!release) sleepMs(1);
    });
    TempRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    for (int i = 0; i < 20; i++) {
        manager.configure("temp-1", "timeout_ms", std::to_string(200 + i));
    }
    CHECK(events.droppedCount() > 0);
    CHECK(manager.readMeasurement("temp-1").ok());
    release = true;
    CHECK(events.waitUntilIdle(3000));
}

TEST(full_transport_buffer_overwrites_oldest_data) {
    VirtualTransport transport(8);
    transport.open();
    transport.feed(std::vector<uint8_t>(20, 0x11));
    CHECK_EQ(transport.pendingBytes(), static_cast<size_t>(8));
    CHECK_EQ(transport.overflowBytes(), 12ULL);
}

TEST(empty_transport_buffer_times_out_cleanly) {
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>();
    TemperatureDevice device("t", "bare", transport, 60);
    device.onRegistered();
    device.open();
    CHECK_CODE(device.readTemperature().status, ErrorCode::Timeout);
    CHECK_EQ(device.state(), DeviceState::Open);
}

TEST(recovery_fails_while_device_is_gone_and_succeeds_when_it_returns) {
    EventManager events;
    DeviceManager manager(events);
    Watcher watcher;
    watcher.attach(events);
    manager.setRecoveryPolicy(3, 1);
    TempRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    rig.simulator->disconnect();
    manager.readMeasurement("temp-1");
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Error);

    unsigned long long before = manager.getStats("temp-1").value.errors;
    CHECK_CODE(manager.recoverDevice("temp-1"), ErrorCode::Disconnected);
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Error);
    CHECK_EQ(manager.getStats("temp-1").value.errors, before + 3);

    rig.simulator->reconnect();
    CHECK_OK(manager.recoverDevice("temp-1"));
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Ready);
    CHECK_OK(manager.openDevice("temp-1"));
    CHECK(manager.readMeasurement("temp-1").ok());

    events.waitUntilIdle(2000);
    CHECK_EQ(watcher.count(EventType::DeviceRecovered), 1);
}

TEST(recovery_is_not_retried_for_state_errors) {
    EventManager events;
    DeviceManager manager(events);
    manager.setRecoveryPolicy(5, 200);
    TempRig rig;
    manager.registerDevice(rig.device);
    long long start = steadyMs();
    CHECK_CODE(manager.recoverDevice("temp-1"), ErrorCode::InvalidState);
    CHECK(steadyMs() - start < 150);
}

TEST(streaming_stops_once_when_the_device_disconnects) {
    EventManager events;
    DeviceManager manager(events);
    Watcher watcher;
    watcher.attach(events);
    TempRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    manager.startEvents("temp-1", 20);
    sleepMs(100);
    rig.simulator->disconnect();
    long long deadline = steadyMs() + 2000;
    while (rig.device->isStreaming() && steadyMs() < deadline) sleepMs(10);
    CHECK(!rig.device->isStreaming());
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Error);
    events.waitUntilIdle(2000);
    CHECK_EQ(watcher.count(EventType::DeviceDisconnected), 1);
    CHECK(watcher.count(EventType::MeasurementAvailable) >= 1);
}

TEST(streaming_reports_bad_frames_and_keeps_going) {
    EventManager events;
    DeviceManager manager(events);
    Watcher watcher;
    watcher.attach(events);
    TempRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    rig.simulator->injectMalformed(3);
    manager.startEvents("temp-1", 15);
    sleepMs(400);
    manager.stopEvents("temp-1");
    events.waitUntilIdle(2000);
    CHECK_EQ(watcher.count(EventType::DeviceError), 3);
    CHECK(watcher.count(EventType::MeasurementAvailable) >= 3);
    CHECK_EQ(manager.getState("temp-1").value, DeviceState::Open);
}

TEST(streaming_can_resume_after_recovery) {
    EventManager events;
    DeviceManager manager(events);
    manager.setRecoveryPolicy(2, 1);
    TempRig rig;
    manager.registerDevice(rig.device);
    manager.openDevice("temp-1");
    manager.startEvents("temp-1", 20);
    sleepMs(60);
    rig.simulator->disconnect();
    sleepMs(150);
    rig.simulator->reconnect();
    CHECK_OK(manager.recoverDevice("temp-1"));
    CHECK_OK(manager.openDevice("temp-1"));
    CHECK_OK(manager.startEvents("temp-1", 20));
    sleepMs(100);
    CHECK(rig.device->isStreaming());
    CHECK(manager.readMeasurement("temp-1").ok());
}

TEST_MAIN("failure injection")
