#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "Rigs.h"
#include "TestHelper.h"

static void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

TEST(parallel_registration_of_distinct_ids) {
    EventManager events(4096);
    DeviceManager manager(events);
    std::vector<std::shared_ptr<TempRig> > rigs;
    for (int t = 0; t < 8; t++) {
        for (int i = 0; i < 25; i++) {
            rigs.push_back(std::make_shared<TempRig>("t" + std::to_string(t) + "-" + std::to_string(i)));
        }
    }
    std::atomic<int> failures(0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.push_back(std::thread([&, t] {
            for (int i = 0; i < 25; i++) {
                if (!manager.registerDevice(rigs[t * 25 + i]->device).ok()) failures++;
            }
        }));
    }
    for (size_t i = 0; i < threads.size(); i++) threads[i].join();
    CHECK_EQ(failures.load(), 0);
    CHECK_EQ(manager.listDevices().size(), static_cast<size_t>(200));
}

TEST(only_one_thread_wins_a_duplicate_id_race) {
    EventManager events;
    DeviceManager manager(events);
    std::vector<std::shared_ptr<TempRig> > rigs;
    for (int i = 0; i < 8; i++) rigs.push_back(std::make_shared<TempRig>("dup"));
    std::atomic<int> successes(0);
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.push_back(std::thread([&, i] {
            if (manager.registerDevice(rigs[i]->device).ok()) successes++;
        }));
    }
    for (size_t i = 0; i < threads.size(); i++) threads[i].join();
    CHECK_EQ(successes.load(), 1);
    CHECK_EQ(manager.listDevices().size(), static_cast<size_t>(1));
}

TEST(lookups_stay_consistent_while_devices_are_added_and_removed) {
    EventManager events(4096);
    DeviceManager manager(events);
    std::vector<std::shared_ptr<TempRig> > rigs;
    for (int i = 0; i < 100; i++) rigs.push_back(std::make_shared<TempRig>("d" + std::to_string(i)));
    std::atomic<bool> done(false);
    std::atomic<int> badInfo(0);

    std::thread writer([&] {
        for (int i = 0; i < 100; i++) manager.registerDevice(rigs[i]->device);
        for (int i = 0; i < 100; i += 2) manager.unregisterDevice("d" + std::to_string(i));
        done = true;
    });
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; r++) {
        readers.push_back(std::thread([&] {
            while (!done) {
                std::vector<DeviceInfo> all = manager.listDevices();
                for (size_t i = 0; i < all.size(); i++) {
                    Result<DeviceInfo> info = manager.getInfo(all[i].id);
                    if (!info.ok() && info.status.code != ErrorCode::NotFound) badInfo++;
                }
            }
        }));
    }
    writer.join();
    for (size_t i = 0; i < readers.size(); i++) readers[i].join();
    CHECK_EQ(badInfo.load(), 0);
    CHECK_EQ(manager.listDevices().size(), static_cast<size_t>(50));
}

TEST(many_clients_read_many_devices_at_once) {
    EventManager events;
    DeviceManager manager(events);
    TempRig temp("temp-1", 500);
    AccelRig accel("accel-1", 500);
    GpsRig gps("gps-1", 500);
    temp.simulator->useWave();
    manager.registerDevice(temp.device);
    manager.registerDevice(accel.device);
    manager.registerDevice(gps.device);
    manager.openDevice("temp-1");
    manager.openDevice("accel-1");
    manager.openDevice("gps-1");

    std::atomic<int> bad(0);
    std::atomic<int> good(0);
    std::vector<std::thread> clients;
    for (int c = 0; c < 4; c++) {
        clients.push_back(std::thread([&] {
            for (int i = 0; i < 40; i++) {
                Result<Measurement> t = manager.readMeasurement("temp-1");
                Result<Measurement> a = manager.readMeasurement("accel-1");
                Result<Measurement> g = manager.readMeasurement("gps-1");
                bool tOk = t.ok() && t.value.valid && t.value.field("temperature") > 21.0 &&
                           t.value.field("temperature") < 29.0;
                bool aOk = a.ok() && a.value.valid && a.value.field("z") > 0.99 && a.value.field("z") < 1.01;
                bool gOk = g.ok() && g.value.valid && g.value.field("latitude") > 12.9 &&
                           g.value.field("latitude") < 13.1;
                if (tOk && aOk && gOk) good++; else bad++;
            }
        }));
    }
    for (size_t i = 0; i < clients.size(); i++) clients[i].join();
    CHECK_EQ(bad.load(), 0);
    CHECK_EQ(good.load(), 160);
    CHECK_EQ(manager.getStats("temp-1").value.reads, 160ULL);
    CHECK_EQ(manager.getStats("temp-1").value.errors, 0ULL);
}

TEST(configuration_changes_while_another_thread_reads) {
    EventManager events;
    DeviceManager manager(events);
    AccelRig accel("accel-1", 500);
    accel.simulator->setFixedValue(0.5, 0.0, 1.0);
    manager.registerDevice(accel.device);
    manager.openDevice("accel-1");

    std::atomic<bool> stop(false);
    std::atomic<int> bad(0);
    std::atomic<int> reads(0);
    std::thread reader([&] {
        while (!stop) {
            Result<Measurement> m = manager.readMeasurement("accel-1");
            double x = m.value.field("x");
            if (!m.ok() || x < 0.49 || x > 0.51) bad++;
            reads++;
        }
    });
    int ranges[] = {2, 4, 8, 16};
    for (int i = 0; i < 100; i++) {
        Status status = manager.configure("accel-1", "range_g", std::to_string(ranges[i % 4]));
        if (!status.ok()) bad++;
    }
    stop = true;
    reader.join();
    CHECK_EQ(bad.load(), 0);
    CHECK(reads.load() > 10);
}

TEST(removing_a_device_while_clients_read_it) {
    EventManager events;
    DeviceManager manager(events);
    TempRig temp("temp-1", 300);
    temp.simulator->useWave();
    manager.registerDevice(temp.device);
    manager.openDevice("temp-1");

    std::atomic<int> unexpected(0);
    std::atomic<int> finished(0);
    std::vector<std::thread> clients;
    for (int c = 0; c < 4; c++) {
        clients.push_back(std::thread([&] {
            while (true) {
                Result<Measurement> m = manager.readMeasurement("temp-1");
                if (m.ok()) continue;
                ErrorCode code = m.status.code;
                if (code != ErrorCode::NotFound && code != ErrorCode::InvalidState) unexpected++;
                break;
            }
            finished++;
        }));
    }
    sleepMs(80);
    CHECK_OK(manager.unregisterDevice("temp-1"));
    for (size_t i = 0; i < clients.size(); i++) clients[i].join();
    CHECK_EQ(finished.load(), 4);
    CHECK_EQ(unexpected.load(), 0);
}

TEST(fast_producer_with_slow_subscriber_drops_but_stays_ordered) {
    EventManager events(16);
    DeviceManager manager(events);
    TempRig temp("temp-1", 300);
    manager.registerDevice(temp.device);

    std::mutex mutex;
    std::vector<unsigned long long> sequences;
    events.subscribe([&](const DeviceEvent& e) {
        if (e.type != EventType::MeasurementAvailable) return;
        sleepMs(30);
        std::lock_guard<std::mutex> lock(mutex);
        sequences.push_back(e.sequence);
    });
    manager.openDevice("temp-1");
    manager.startEvents("temp-1", 10);
    sleepMs(700);
    manager.stopEvents("temp-1");
    CHECK(events.waitUntilIdle(5000));

    CHECK(events.droppedCount() > 0);
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(sequences.size() >= 5);
    for (size_t i = 1; i < sequences.size(); i++) CHECK(sequences[i] > sequences[i - 1]);
}

TEST(cancelling_a_subscription_with_events_queued) {
    EventManager events(500);
    std::atomic<int> received(0);
    int id = events.subscribe([&](const DeviceEvent&) {
        received++;
        sleepMs(1);
    });
    for (int i = 0; i < 200; i++) {
        DeviceEvent e;
        e.deviceId = "d";
        events.publish(e);
    }
    while (received.load() < 10) sleepMs(1);
    CHECK(events.unsubscribe(id));
    int atCancel = received.load();
    CHECK(events.waitUntilIdle(5000));
    CHECK_EQ(received.load(), atCancel);
}

TEST(several_producers_publish_without_losing_events) {
    EventManager events(5000);
    std::atomic<int> received(0);
    std::mutex mutex;
    std::vector<unsigned long long> sequences;
    events.subscribe([&](const DeviceEvent& e) {
        received++;
        std::lock_guard<std::mutex> lock(mutex);
        sequences.push_back(e.sequence);
    });
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; p++) {
        producers.push_back(std::thread([&events] {
            for (int i = 0; i < 250; i++) {
                DeviceEvent e;
                e.deviceId = "d";
                events.publish(e);
            }
        }));
    }
    for (size_t i = 0; i < producers.size(); i++) producers[i].join();
    CHECK(events.waitUntilIdle(5000));
    CHECK_EQ(received.load(), 1000);
    CHECK_EQ(events.droppedCount(), 0ULL);
    std::lock_guard<std::mutex> lock(mutex);
    for (size_t i = 0; i < sequences.size(); i++) CHECK_EQ(sequences[i], static_cast<unsigned long long>(i + 1));
}

TEST(shutdown_while_readers_are_waiting_does_not_hang) {
    EventManager events;
    DeviceManager manager(events);
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>();
    std::shared_ptr<TemperatureDevice> device = std::make_shared<TemperatureDevice>("quiet", "no data", transport, 400);
    manager.registerDevice(device);
    manager.openDevice("quiet");

    std::atomic<int> returned(0);
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; i++) {
        readers.push_back(std::thread([&] {
            Result<Measurement> m = manager.readMeasurement("quiet");
            if (!m.ok()) returned++;
        }));
    }
    sleepMs(50);
    long long start = steadyMs();
    manager.shutdown();
    CHECK(steadyMs() - start < 3000);
    for (size_t i = 0; i < readers.size(); i++) readers[i].join();
    CHECK_EQ(returned.load(), 3);
    CHECK_EQ(device->state(), DeviceState::Closed);
}

TEST(shutdown_while_streaming_stops_all_workers) {
    EventManager events;
    DeviceManager manager(events);
    TempRig temp("temp-1");
    AccelRig accel("accel-1");
    manager.registerDevice(temp.device);
    manager.registerDevice(accel.device);
    manager.openDevice("temp-1");
    manager.openDevice("accel-1");
    manager.startEvents("temp-1", 10);
    manager.startEvents("accel-1", 10);
    sleepMs(100);
    manager.shutdown();
    CHECK(!temp.device->isStreaming());
    CHECK(!accel.device->isStreaming());
    CHECK(events.waitUntilIdle(2000));
}

TEST(parallel_writers_do_not_corrupt_counters) {
    EventManager events;
    DeviceManager manager(events);
    VdevRig vdev("vdev-1", 300);
    manager.registerDevice(vdev.device);
    manager.openDevice("vdev-1");
    std::atomic<int> failed(0);
    std::vector<std::thread> writers;
    for (int w = 0; w < 4; w++) {
        writers.push_back(std::thread([&] {
            for (int i = 0; i < 50; i++) {
                if (!manager.writeBytes("vdev-1", toBytes("line\n")).ok()) failed++;
            }
        }));
    }
    for (size_t i = 0; i < writers.size(); i++) writers[i].join();
    CHECK_EQ(failed.load(), 0);
    DeviceStats stats = manager.getStats("vdev-1").value;
    CHECK_EQ(stats.writes, 200ULL);
    CHECK_EQ(stats.bytesWritten, 1000ULL);
}

TEST_MAIN("concurrency")
