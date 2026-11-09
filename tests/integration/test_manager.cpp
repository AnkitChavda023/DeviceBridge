#include <chrono>
#include <mutex>
#include <thread>
#include "Rigs.h"
#include "TestHelper.h"
#include "devices/Parsers.h"

struct Collector {
    std::mutex mutex;
    std::vector<DeviceEvent> events;
    EventManager* source = nullptr;
    int subscription = 0;

    ~Collector() {
        if (source != nullptr) {
            source->unsubscribe(subscription);
        }
    }
    void attach(EventManager& manager, const std::string& filter = "") {
        source = &manager;
        subscription = manager.subscribe(
            [this](const DeviceEvent& e) {
                std::lock_guard<std::mutex> lock(mutex);
                events.push_back(e);
            },
            filter);
    }
    size_t count(EventType type) {
        std::lock_guard<std::mutex> lock(mutex);
        size_t n = 0;
        for (size_t i = 0; i < events.size(); i++) {
            if (events[i].type == type) n++;
        }
        return n;
    }
    std::vector<EventType> types() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<EventType> result;
        for (size_t i = 0; i < events.size(); i++) result.push_back(events[i].type);
        return result;
    }
};

struct Fleet {
    EventManager events;
    DeviceManager manager;
    TempRig temp;
    AccelRig accel;
    GpsRig gps;
    VdevRig vdev;

    Fleet() : manager(events), temp("temp-1"), accel("accel-1"), gps("gps-1"), vdev("vdev-1") {}

    void registerAll() {
        manager.registerDevice(temp.device);
        manager.registerDevice(accel.device);
        manager.registerDevice(gps.device);
        manager.registerDevice(vdev.device);
    }
};

TEST(register_list_and_find_devices) {
    Fleet f;
    f.registerAll();
    CHECK_EQ(f.manager.listDevices().size(), static_cast<size_t>(4));
    CHECK_EQ(f.manager.findByType(DeviceType::Gps).size(), static_cast<size_t>(1));
    CHECK_EQ(f.manager.findByType(DeviceType::Serial).size(), static_cast<size_t>(0));
    Result<DeviceInfo> info = f.manager.getInfo("gps-1");
    CHECK(info.ok());
    CHECK_EQ(info.value.type, DeviceType::Gps);
    CHECK_CODE(f.manager.getInfo("nope").status, ErrorCode::NotFound);
    CHECK_EQ(f.manager.getState("gps-1").value, DeviceState::Ready);
}

TEST(duplicate_and_invalid_registrations_fail) {
    Fleet f;
    CHECK_OK(f.manager.registerDevice(f.temp.device));
    CHECK_CODE(f.manager.registerDevice(f.temp.device), ErrorCode::AlreadyExists);
    TempRig badId("bad id");
    CHECK_CODE(f.manager.registerDevice(badId.device), ErrorCode::InvalidArgument);
    CHECK_CODE(f.manager.registerDevice(std::shared_ptr<IDevice>()), ErrorCode::InvalidArgument);
    CHECK_EQ(f.manager.listDevices().size(), static_cast<size_t>(1));
}

TEST(read_every_device_type_through_the_manager) {
    Fleet f;
    f.registerAll();
    f.temp.simulator->setFixedValue(24.5);
    f.accel.simulator->setFixedValue(0.5, -0.25, 1.0);
    CHECK_OK(f.manager.openDevice("temp-1"));
    CHECK_OK(f.manager.openDevice("accel-1"));
    CHECK_OK(f.manager.openDevice("gps-1"));
    CHECK_OK(f.manager.openDevice("vdev-1"));

    Result<Measurement> t = f.manager.readMeasurement("temp-1");
    CHECK(t.ok());
    CHECK_NEAR(t.value.field("temperature"), 24.5, 0.01);
    CHECK(t.value.valid);
    CHECK(t.value.timestampMs > 0);
    CHECK_EQ(t.value.unit, std::string("celsius"));

    Result<Measurement> a = f.manager.readMeasurement("accel-1");
    CHECK(a.ok());
    CHECK_NEAR(a.value.field("x"), 0.5, 0.01);
    CHECK_NEAR(a.value.field("y"), -0.25, 0.01);
    CHECK_NEAR(a.value.field("z"), 1.0, 0.01);

    Result<Measurement> g = f.manager.readMeasurement("gps-1");
    CHECK(g.ok());
    CHECK(g.value.valid);
    CHECK_NEAR(g.value.field("latitude"), 12.97, 0.01);
    CHECK_NEAR(g.value.field("longitude"), 77.59, 0.01);

    Result<Measurement> v = f.manager.readMeasurement("vdev-1");
    CHECK(v.ok());
    CHECK_NEAR(v.value.field("seq"), 1.0, 0.001);
    CHECK_NEAR(v.value.field("value"), 20.05, 0.001);
}

TEST(operations_before_open_are_rejected) {
    Fleet f;
    f.registerAll();
    CHECK_CODE(f.manager.readMeasurement("temp-1").status, ErrorCode::InvalidState);
    CHECK_CODE(f.manager.readBytes("gps-1", 10, 50).status, ErrorCode::InvalidState);
    CHECK_CODE(f.manager.writeBytes("vdev-1", toBytes("x")).status, ErrorCode::InvalidState);
    CHECK_CODE(f.manager.startEvents("temp-1", 50), ErrorCode::InvalidState);
    CHECK_CODE(f.manager.resetDevice("temp-1"), ErrorCode::InvalidState);
    CHECK_CODE(f.manager.closeDevice("temp-1"), ErrorCode::InvalidState);
}

TEST(unknown_device_ids_return_not_found) {
    Fleet f;
    CHECK_CODE(f.manager.openDevice("x"), ErrorCode::NotFound);
    CHECK_CODE(f.manager.closeDevice("x"), ErrorCode::NotFound);
    CHECK_CODE(f.manager.readMeasurement("x").status, ErrorCode::NotFound);
    CHECK_CODE(f.manager.configure("x", "a", "b"), ErrorCode::NotFound);
    CHECK_CODE(f.manager.unregisterDevice("x"), ErrorCode::NotFound);
    CHECK_CODE(f.manager.recoverDevice("x"), ErrorCode::NotFound);
    CHECK_CODE(f.manager.getStats("x").status, ErrorCode::NotFound);
}

TEST(capabilities_are_checked_before_operations) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("temp-1");
    f.manager.openDevice("gps-1");
    CHECK_CODE(f.manager.readBytes("temp-1", 4, 50).status, ErrorCode::Unsupported);
    CHECK_CODE(f.manager.writeBytes("temp-1", toBytes("x")).status, ErrorCode::Unsupported);
    CHECK_CODE(f.manager.writeBytes("gps-1", toBytes("x")).status, ErrorCode::Unsupported);
    CHECK_CODE(f.manager.waitReadable("temp-1", 10).status, ErrorCode::Unsupported);
    CHECK(f.manager.readBytes("gps-1", 4, 200).ok());
}

TEST(configuration_is_validated_and_reported) {
    Fleet f;
    f.registerAll();
    CHECK_OK(f.manager.configure("temp-1", "timeout_ms", "300"));
    CHECK_CODE(f.manager.configure("temp-1", "timeout_ms", "5"), ErrorCode::InvalidArgument);
    CHECK_CODE(f.manager.configure("temp-1", "timeout_ms", "abc"), ErrorCode::InvalidArgument);
    CHECK_CODE(f.manager.configure("temp-1", "sample_interval_ms", "999999"), ErrorCode::InvalidArgument);
    CHECK_CODE(f.manager.configure("temp-1", "colour", "red"), ErrorCode::Unsupported);
    CHECK_CODE(f.manager.configure("accel-1", "range_g", "3"), ErrorCode::InvalidArgument);

    Result<std::map<std::string, std::string> > config = f.manager.getConfig("temp-1");
    CHECK(config.ok());
    CHECK_EQ(config.value["timeout_ms"], std::string("300"));
    Result<std::vector<std::string> > keys = f.manager.getSupportedKeys("accel-1");
    CHECK_EQ(keys.value.size(), static_cast<size_t>(3));
}

TEST(accelerometer_range_change_keeps_readings_correct) {
    Fleet f;
    f.registerAll();
    f.accel.simulator->setFixedValue(1.5, 0.0, -1.0);
    f.manager.openDevice("accel-1");
    CHECK_OK(f.manager.configure("accel-1", "range_g", "8"));
    Result<Measurement> m = f.manager.readMeasurement("accel-1");
    CHECK(m.ok());
    CHECK_NEAR(m.value.field("x"), 1.5, 0.01);
    CHECK_NEAR(m.value.field("z"), -1.0, 0.01);
    CHECK_EQ(f.accel.simulator->range(), 8);
    CHECK_EQ(f.manager.getConfig("accel-1").value["range_g"], std::string("8"));
}

TEST(range_set_before_open_is_applied_on_open) {
    Fleet f;
    f.registerAll();
    f.accel.simulator->setFixedValue(3.0, 0.0, 0.0);
    CHECK_OK(f.manager.configure("accel-1", "range_g", "4"));
    f.manager.openDevice("accel-1");
    CHECK_EQ(f.accel.simulator->range(), 4);
    Result<Measurement> m = f.manager.readMeasurement("accel-1");
    CHECK_NEAR(m.value.field("x"), 3.0, 0.01);
}

TEST(lifecycle_events_are_published_in_order) {
    Fleet f;
    Collector collector;
    collector.attach(f.events);
    f.manager.registerDevice(f.temp.device);
    f.manager.openDevice("temp-1");
    f.manager.configure("temp-1", "timeout_ms", "250");
    f.manager.closeDevice("temp-1");
    f.manager.unregisterDevice("temp-1");
    f.events.waitUntilIdle(2000);
    std::vector<EventType> types = collector.types();
    CHECK_EQ(types.size(), static_cast<size_t>(5));
    if (types.size() == 5) {
        CHECK_EQ(types[0], EventType::DeviceRegistered);
        CHECK_EQ(types[1], EventType::DeviceOpened);
        CHECK_EQ(types[2], EventType::ConfigurationChanged);
        CHECK_EQ(types[3], EventType::DeviceClosed);
        CHECK_EQ(types[4], EventType::DeviceRemoved);
    }
}

TEST(unregister_closes_an_open_device) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("temp-1");
    f.manager.startEvents("temp-1", 20);
    std::shared_ptr<IDevice> held = f.temp.device;
    CHECK_OK(f.manager.unregisterDevice("temp-1"));
    CHECK_EQ(held->state(), DeviceState::Closed);
    CHECK_CODE(f.manager.getInfo("temp-1").status, ErrorCode::NotFound);
    CHECK_EQ(f.manager.listDevices().size(), static_cast<size_t>(3));
    CHECK(!f.temp.device->isStreaming());
}

TEST(counters_are_exposed_through_the_manager) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("vdev-1");
    f.manager.writeBytes("vdev-1", toBytes("hello\n"));
    f.manager.readBytes("vdev-1", 100, 100);
    Result<DeviceStats> stats = f.manager.getStats("vdev-1");
    CHECK(stats.ok());
    CHECK_EQ(stats.value.writes, 1ULL);
    CHECK_EQ(stats.value.bytesWritten, 6ULL);
    CHECK_EQ(stats.value.reads, 1ULL);
    CHECK(stats.value.bytesRead >= 6);
    CHECK_EQ(stats.value.errors, 0ULL);
}

TEST(virtual_device_loops_back_written_bytes) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("vdev-1");
    CHECK(f.manager.writeBytes("vdev-1", toBytes("hello\n")).ok());
    Result<bool> readable = f.manager.waitReadable("vdev-1", 100);
    CHECK(readable.ok());
    CHECK(readable.value);
    Result<std::vector<uint8_t> > data = f.manager.readBytes("vdev-1", 100, 100);
    CHECK(data.ok());
    CHECK_EQ(std::string(data.value.begin(), data.value.begin() + 6), std::string("hello\n"));
}

TEST(gps_raw_stream_is_readable) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("gps-1");
    Result<std::vector<uint8_t> > data = f.manager.readBytes("gps-1", 200, 200);
    CHECK(data.ok());
    CHECK_EQ(data.value[0], static_cast<uint8_t>('$'));
    CHECK_CODE(f.manager.readBytes("gps-1", 0, 10).status, ErrorCode::InvalidArgument);
}

TEST(event_streaming_delivers_measurements_until_stopped) {
    Fleet f;
    f.registerAll();
    Collector collector;
    collector.attach(f.events, "temp-1");
    f.manager.openDevice("temp-1");
    CHECK_CODE(f.manager.startEvents("temp-1", 5), ErrorCode::InvalidArgument);
    CHECK_OK(f.manager.startEvents("temp-1", 20));
    CHECK_CODE(f.manager.startEvents("temp-1", 20), ErrorCode::Busy);
    CHECK(f.temp.device->isStreaming());

    long long deadline = steadyMs() + 3000;
    while (collector.count(EventType::MeasurementAvailable) < 5 && steadyMs() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(collector.count(EventType::MeasurementAvailable) >= 5);

    CHECK_OK(f.manager.stopEvents("temp-1"));
    CHECK(!f.temp.device->isStreaming());
    f.events.waitUntilIdle(1000);
    size_t after = collector.count(EventType::MeasurementAvailable);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(collector.count(EventType::MeasurementAvailable), after);
}

TEST(streaming_events_carry_measurements_from_several_devices) {
    Fleet f;
    f.registerAll();
    Collector collector;
    collector.attach(f.events);
    f.manager.openDevice("temp-1");
    f.manager.openDevice("accel-1");
    f.manager.startEvents("temp-1", 20);
    f.manager.startEvents("accel-1", 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    f.manager.stopEvents("temp-1");
    f.manager.stopEvents("accel-1");
    f.events.waitUntilIdle(1000);
    bool sawTemp = false;
    bool sawAccel = false;
    std::lock_guard<std::mutex> lock(collector.mutex);
    for (size_t i = 0; i < collector.events.size(); i++) {
        const DeviceEvent& e = collector.events[i];
        if (e.type == EventType::MeasurementAvailable) {
            CHECK(e.hasMeasurement);
            CHECK(e.measurement.valid);
            if (e.deviceId == "temp-1") sawTemp = true;
            if (e.deviceId == "accel-1") sawAccel = true;
        }
    }
    CHECK(sawTemp);
    CHECK(sawAccel);
}

TEST(sample_interval_change_applies_to_running_stream) {
    Fleet f;
    f.registerAll();
    Collector collector;
    collector.attach(f.events, "temp-1");
    f.manager.openDevice("temp-1");
    f.manager.startEvents("temp-1", 1000);
    f.manager.configure("temp-1", "sample_interval_ms", "20");
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    f.manager.stopEvents("temp-1");
    f.events.waitUntilIdle(1000);
    CHECK(collector.count(EventType::MeasurementAvailable) >= 5);
}

TEST(reset_works_on_devices_that_support_it) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("temp-1");
    f.manager.openDevice("accel-1");
    f.manager.openDevice("vdev-1");
    CHECK_OK(f.manager.resetDevice("temp-1"));
    CHECK_OK(f.manager.resetDevice("accel-1"));
    CHECK_OK(f.manager.resetDevice("vdev-1"));
}

TEST(shutdown_closes_and_removes_everything) {
    Fleet f;
    f.registerAll();
    f.manager.openDevice("temp-1");
    f.manager.startEvents("temp-1", 20);
    f.manager.openDevice("gps-1");
    f.manager.shutdown();
    CHECK_EQ(f.manager.listDevices().size(), static_cast<size_t>(0));
    CHECK_EQ(f.temp.device->state(), DeviceState::Closed);
    CHECK_EQ(f.gps.device->state(), DeviceState::Closed);
}

TEST_MAIN("integration: device manager")
