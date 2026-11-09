#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "devices/AccelerometerDevice.h"
#include "devices/TemperatureDevice.h"
#include "devices/VirtualSensorDevice.h"
#include "events/EventManager.h"
#include "manager/DeviceManager.h"
#include "simulator/Simulators.h"
#include "util/Logger.h"
#include "util/Util.h"

typedef std::chrono::steady_clock Clock;

static long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

static void report(const std::string& name, std::vector<double> micros) {
    if (micros.empty()) {
        return;
    }
    std::sort(micros.begin(), micros.end());
    double sum = 0;
    for (size_t i = 0; i < micros.size(); i++) {
        sum += micros[i];
    }
    size_t n = micros.size();
    std::printf("| %-34s | %8zu | %9.2f | %9.2f | %9.2f | %9.2f | %9.2f |\n", name.c_str(), n, sum / n,
                micros[n / 2], micros[n * 95 / 100], micros[n * 99 / 100], micros[n - 1]);
}

static long residentKb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            return std::atol(line.c_str() + 6);
        }
    }
    return -1;
}

struct Bench {
    EventManager events;
    DeviceManager manager;
    Bench(size_t queue = 4096) : events(queue), manager(events) {}
};

static void benchReadLatency(int iterations) {
    Bench b;
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>(256);
    TemperatureSimulator simulator(transport);
    b.manager.registerDevice(std::make_shared<TemperatureDevice>("temp", "temp", transport, 500));
    b.manager.openDevice("temp");
    std::vector<double> micros;
    for (int i = 0; i < iterations; i++) {
        long long start = nowNs();
        Result<Measurement> m = b.manager.readMeasurement("temp");
        long long end = nowNs();
        if (m.ok()) {
            micros.push_back((end - start) / 1000.0);
        }
    }
    report("measurement read (temperature)", micros);
}

static void benchWriteLatency(int iterations) {
    Bench b;
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>(4096);
    VirtualDeviceSimulator simulator(transport);
    b.manager.registerDevice(std::make_shared<VirtualSensorDevice>("vdev", "vdev", transport, 500));
    b.manager.openDevice("vdev");
    std::vector<uint8_t> data(64, 'a');
    std::vector<double> micros;
    for (int i = 0; i < iterations; i++) {
        long long start = nowNs();
        Result<size_t> r = b.manager.writeBytes("vdev", data);
        long long end = nowNs();
        if (r.ok()) {
            micros.push_back((end - start) / 1000.0);
        }
        if (i % 50 == 49) {
            b.manager.readBytes("vdev", 4096, 10);
        }
    }
    report("stream write 64 B (virtual device)", micros);
}

static void benchEventLatency(int count, int gapMicros, const std::string& label) {
    EventManager events(count + 10);
    std::vector<double> micros;
    events.subscribe([&micros](const DeviceEvent& e) {
        micros.push_back((nowNs() - std::atol(e.message.c_str())) / 1000.0);
    });
    for (int i = 0; i < count; i++) {
        DeviceEvent e;
        e.deviceId = "d";
        e.message = std::to_string(nowNs());
        events.publish(e);
        if (gapMicros > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(gapMicros));
        }
    }
    events.waitUntilIdle(30000);
    report(label, micros);
}

static void benchThroughput(int seconds) {
    Bench b;
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>(256);
    TemperatureSimulator simulator(transport);
    b.manager.registerDevice(std::make_shared<TemperatureDevice>("temp", "temp", transport, 500));
    b.manager.openDevice("temp");
    long long deadline = nowNs() + static_cast<long long>(seconds) * 1000000000LL;
    long long operations = 0;
    while (nowNs() < deadline) {
        b.manager.readMeasurement("temp");
        operations++;
    }
    std::printf("single client throughput: %.0f reads/s (%.2f MB/s of raw frames)\n",
                static_cast<double>(operations) / seconds, operations * 4.0 / seconds / 1e6);
}

static void benchClients(int clients, bool sharedDevice, int seconds) {
    Bench b;
    std::vector<std::shared_ptr<VirtualTransport> > transports;
    std::vector<std::shared_ptr<TemperatureSimulator> > simulators;
    int deviceCount = sharedDevice ? 1 : clients;
    for (int i = 0; i < deviceCount; i++) {
        transports.push_back(std::make_shared<VirtualTransport>(256));
        simulators.push_back(std::make_shared<TemperatureSimulator>(transports[i]));
        b.manager.registerDevice(std::make_shared<TemperatureDevice>("t" + std::to_string(i), "t", transports[i], 500));
        b.manager.openDevice("t" + std::to_string(i));
    }
    std::atomic<long long> total(0);
    std::atomic<bool> stop(false);
    std::vector<std::thread> threads;
    for (int c = 0; c < clients; c++) {
        std::string id = "t" + std::to_string(sharedDevice ? 0 : c);
        threads.push_back(std::thread([&, id] {
            long long local = 0;
            while (!stop) {
                if (b.manager.readMeasurement(id).ok()) {
                    local++;
                }
            }
            total += local;
        }));
    }
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop = true;
    for (size_t i = 0; i < threads.size(); i++) {
        threads[i].join();
    }
    std::printf("| %-8d | %-14s | %12.0f |\n", clients, sharedDevice ? "one shared" : "one each",
                static_cast<double>(total.load()) / seconds);
}

static void benchOverload() {
    EventManager events(64);
    std::atomic<int> received(0);
    events.subscribe([&received](const DeviceEvent&) {
        received++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    int accepted = 0;
    for (int i = 0; i < 5000; i++) {
        DeviceEvent e;
        if (events.publish(e)) {
            accepted++;
        }
    }
    events.waitUntilIdle(30000);
    std::printf("queue overload: published 5000, accepted %d, dropped %llu, delivered %d (capacity 64, 1 ms subscriber)\n",
                accepted, events.droppedCount(), received.load());
}

static void benchRecovery() {
    Bench b;
    b.manager.setRecoveryPolicy(3, 1);
    std::shared_ptr<VirtualTransport> transport = std::make_shared<VirtualTransport>(256);
    TemperatureSimulator simulator(transport);
    b.manager.registerDevice(std::make_shared<TemperatureDevice>("temp", "temp", transport, 100));
    std::vector<double> micros;
    for (int i = 0; i < 200; i++) {
        b.manager.openDevice("temp");
        simulator.disconnect();
        b.manager.readMeasurement("temp");
        simulator.reconnect();
        long long start = nowNs();
        Status s = b.manager.recoverDevice("temp");
        long long end = nowNs();
        if (s.ok()) {
            micros.push_back((end - start) / 1000.0);
        }
    }
    report("recovery after reconnect", micros);
}

static void benchMemory() {
    long before = residentKb();
    std::vector<std::shared_ptr<TemperatureDevice> > devices;
    std::vector<std::shared_ptr<TemperatureSimulator> > simulators;
    std::vector<std::shared_ptr<VirtualTransport> > transports;
    EventManager events;
    DeviceManager manager(events);
    for (int i = 0; i < 100; i++) {
        transports.push_back(std::make_shared<VirtualTransport>(256));
        simulators.push_back(std::make_shared<TemperatureSimulator>(transports[i]));
        devices.push_back(std::make_shared<TemperatureDevice>("m" + std::to_string(i), "m", transports[i], 500));
        manager.registerDevice(devices[i]);
        manager.openDevice("m" + std::to_string(i));
        manager.startEvents("m" + std::to_string(i), 1000);
    }
    long after = residentKb();
    std::printf("memory: resident set %ld kB before, %ld kB after 100 open streaming devices (%.1f kB per device incl. worker thread)\n",
                before, after, (after - before) / 100.0);
    manager.shutdown();
}

int main(int argc, char** argv) {
    Logger::setLevel(LogLevel::Off);
    bool quick = argc > 1 && std::string(argv[1]) == "--quick";
    int iterations = quick ? 2000 : 50000;
    int seconds = quick ? 1 : 3;

    std::printf("## Latency in microseconds\n\n");
    std::printf("| %-34s | %8s | %9s | %9s | %9s | %9s | %9s |\n", "operation", "samples", "mean", "p50", "p95", "p99", "max");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|\n", std::string(36, '-').c_str(), std::string(10, '-').c_str(),
                std::string(11, '-').c_str(), std::string(11, '-').c_str(), std::string(11, '-').c_str(),
                std::string(11, '-').c_str(), std::string(11, '-').c_str());
    benchReadLatency(iterations);
    benchWriteLatency(iterations);
    benchEventLatency(quick ? 2000 : 20000, 0, "event delivery, burst publish");
    benchEventLatency(quick ? 500 : 2000, 1000, "event delivery, 1 event per ms");
    benchRecovery();

    std::printf("\n## Throughput\n\n");
    benchThroughput(seconds);
    std::printf("\n| clients  | devices        | total reads/s |\n|----------|----------------|---------------|\n");
    int counts[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) {
        benchClients(counts[i], false, seconds);
    }
    for (int i = 0; i < 4; i++) {
        benchClients(counts[i], true, seconds);
    }

    std::printf("\n## Overload and memory\n\n");
    benchOverload();
    benchMemory();
    return 0;
}
