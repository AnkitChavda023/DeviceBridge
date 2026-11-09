#include <atomic>
#include <chrono>
#include <thread>
#include "TestHelper.h"
#include "events/EventManager.h"

static DeviceEvent makeEvent(const std::string& id, const std::string& message = "") {
    DeviceEvent event;
    event.type = EventType::DeviceOpened;
    event.deviceId = id;
    event.message = message;
    return event;
}

TEST(events_are_delivered_in_order) {
    EventManager manager(100);
    std::vector<unsigned long long> sequences;
    manager.subscribe([&sequences](const DeviceEvent& e) { sequences.push_back(e.sequence); });
    for (int i = 0; i < 50; i++) {
        CHECK(manager.publish(makeEvent("d")));
    }
    CHECK(manager.waitUntilIdle(2000));
    CHECK_EQ(sequences.size(), static_cast<size_t>(50));
    for (size_t i = 1; i < sequences.size(); i++) {
        CHECK(sequences[i] == sequences[i - 1] + 1);
    }
    CHECK_EQ(manager.deliveredCount(), 50ULL);
}

TEST(device_filter_limits_delivery) {
    EventManager manager;
    std::atomic<int> all(0);
    std::atomic<int> onlyA(0);
    manager.subscribe([&all](const DeviceEvent&) { all++; });
    manager.subscribe([&onlyA](const DeviceEvent&) { onlyA++; }, "a");
    manager.publish(makeEvent("a"));
    manager.publish(makeEvent("b"));
    manager.publish(makeEvent("a"));
    manager.waitUntilIdle(2000);
    CHECK_EQ(all.load(), 3);
    CHECK_EQ(onlyA.load(), 2);
}

TEST(unsubscribe_stops_delivery) {
    EventManager manager;
    std::atomic<int> count(0);
    int id = manager.subscribe([&count](const DeviceEvent&) { count++; });
    manager.publish(makeEvent("a"));
    manager.waitUntilIdle(2000);
    CHECK_EQ(count.load(), 1);
    CHECK(manager.unsubscribe(id));
    CHECK(!manager.unsubscribe(id));
    manager.publish(makeEvent("a"));
    manager.waitUntilIdle(2000);
    CHECK_EQ(count.load(), 1);
}

TEST(full_queue_drops_new_events_and_counts_them) {
    EventManager manager(5);
    std::atomic<bool> release(false);
    std::atomic<int> received(0);
    manager.subscribe([&](const DeviceEvent&) {
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        received++;
    });
    int accepted = 0;
    for (int i = 0; i < 40; i++) {
        if (manager.publish(makeEvent("a"))) {
            accepted++;
        }
    }
    CHECK(accepted <= 6);
    CHECK_EQ(manager.droppedCount(), static_cast<unsigned long long>(40 - accepted));
    release = true;
    CHECK(manager.waitUntilIdle(3000));
    CHECK_EQ(received.load(), accepted);
}

TEST(stop_delivers_pending_events_then_rejects_new_ones) {
    EventManager manager(100);
    std::atomic<int> count(0);
    manager.subscribe([&count](const DeviceEvent&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        count++;
    });
    for (int i = 0; i < 20; i++) {
        manager.publish(makeEvent("a"));
    }
    manager.stop();
    CHECK_EQ(count.load(), 20);
    CHECK(!manager.publish(makeEvent("a")));
}

TEST(throwing_callback_does_not_kill_dispatcher) {
    EventManager manager;
    std::atomic<int> count(0);
    manager.subscribe([](const DeviceEvent&) { throw 1; });
    manager.subscribe([&count](const DeviceEvent&) { count++; });
    manager.publish(makeEvent("a"));
    manager.publish(makeEvent("a"));
    manager.waitUntilIdle(2000);
    CHECK_EQ(count.load(), 2);
}

TEST(callback_may_unsubscribe_itself) {
    EventManager manager;
    std::atomic<int> count(0);
    int id = 0;
    id = manager.subscribe([&](const DeviceEvent&) {
        count++;
        manager.unsubscribe(id);
    });
    manager.publish(makeEvent("a"));
    manager.publish(makeEvent("a"));
    manager.waitUntilIdle(2000);
    CHECK_EQ(count.load(), 1);
}

TEST(timestamp_and_description) {
    EventManager manager;
    DeviceEvent seen;
    manager.subscribe([&seen](const DeviceEvent& e) { seen = e; });
    manager.publish(makeEvent("dev", "hello"));
    manager.waitUntilIdle(2000);
    CHECK(seen.timestampMs > 0);
    CHECK(describeEvent(seen).find("DeviceOpened device=dev hello") != std::string::npos);
}

TEST_MAIN("unit: event manager")
