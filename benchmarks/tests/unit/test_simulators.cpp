#include <chrono>
#include <thread>
#include "Rigs.h"
#include "TestHelper.h"
#include "devices/Parsers.h"

TEST(temperature_simulator_fixed_value) {
    auto transport = std::make_shared<VirtualTransport>();
    TemperatureSimulator sim(transport);
    sim.setFixedValue(31.25);
    transport->open();
    sim.generateOne();
    Result<std::vector<uint8_t> > bytes = transport->read(16, 50);
    Result<TemperatureMeasurement> m = parseTemperatureFrame(bytes.value, 0);
    CHECK(m.ok());
    CHECK_NEAR(m.value.celsius, 31.25, 0.01);
}

TEST(temperature_simulator_wave_stays_in_range_and_changes) {
    auto transport = std::make_shared<VirtualTransport>(1024);
    TemperatureSimulator sim(transport);
    transport->open();
    double low = 1000;
    double high = -1000;
    for (int i = 0; i < 40; i++) {
        sim.generateOne();
        Result<TemperatureMeasurement> m = parseTemperatureFrame(transport->read(4, 50).value, 0);
        CHECK(m.ok());
        if (m.value.celsius < low) low = m.value.celsius;
        if (m.value.celsius > high) high = m.value.celsius;
    }
    CHECK(low >= 21.9);
    CHECK(high <= 28.1);
    CHECK(high - low > 3.0);
}

TEST(simulator_is_repeatable) {
    auto t1 = std::make_shared<VirtualTransport>();
    auto t2 = std::make_shared<VirtualTransport>();
    TemperatureSimulator a(t1);
    TemperatureSimulator b(t2);
    t1->open();
    t2->open();
    for (int i = 0; i < 5; i++) {
        a.generateOne();
        b.generateOne();
        CHECK_EQ(toHex(t1->read(4, 20).value), toHex(t2->read(4, 20).value));
    }
}

TEST(accelerometer_simulator_obeys_range_command) {
    auto transport = std::make_shared<VirtualTransport>();
    AccelerometerSimulator sim(transport);
    transport->open();
    CHECK_EQ(sim.range(), 2);
    std::vector<uint8_t> command;
    command.push_back(kAccelSetRangeCommand);
    command.push_back(8);
    transport->write(command);
    CHECK_EQ(sim.range(), 8);
    command[1] = 7;
    transport->write(command);
    CHECK_EQ(sim.range(), 8);
}

TEST(gps_simulator_produces_valid_sentences) {
    auto transport = std::make_shared<VirtualTransport>(1024);
    GpsSimulator sim(transport);
    transport->open();
    int gga = 0;
    int rmc = 0;
    for (int i = 0; i < 6; i++) {
        sim.generateOne();
        std::vector<uint8_t> bytes = transport->read(200, 50).value;
        std::string line(bytes.begin(), bytes.end());
        CHECK(line.size() > 10);
        CHECK_EQ(line.substr(line.size() - 2), std::string("\r\n"));
        Result<GpsMeasurement> m = parseNmea(line.substr(0, line.size() - 2), 0);
        CHECK(m.ok());
        CHECK(m.value.fixValid);
        CHECK_NEAR(m.value.latitude, 12.97, 0.01);
        CHECK_NEAR(m.value.longitude, 77.59, 0.01);
        if (line.find("GPGGA") != std::string::npos) gga++;
        if (line.find("GPRMC") != std::string::npos) rmc++;
    }
    CHECK_EQ(gga, 3);
    CHECK_EQ(rmc, 3);
}

TEST(gps_simulator_can_lose_fix) {
    auto transport = std::make_shared<VirtualTransport>();
    GpsSimulator sim(transport);
    sim.setFix(false);
    transport->open();
    sim.generateOne();
    std::vector<uint8_t> bytes = transport->read(200, 50).value;
    std::string line(bytes.begin(), bytes.end());
    Result<GpsMeasurement> m = parseNmea(line.substr(0, line.size() - 2), 0);
    CHECK(m.ok());
    CHECK(!m.value.fixValid);
}

TEST(malformed_injection_corrupts_exactly_n_frames) {
    auto transport = std::make_shared<VirtualTransport>(1024);
    TemperatureSimulator sim(transport);
    sim.injectMalformed(2);
    transport->open();
    int bad = 0;
    for (int i = 0; i < 5; i++) {
        sim.generateOne();
        if (!parseTemperatureFrame(transport->read(4, 20).value, 0).ok()) {
            bad++;
        }
    }
    CHECK_EQ(bad, 2);
}

TEST(gps_malformed_injection_uses_both_variants) {
    auto transport = std::make_shared<VirtualTransport>(1024);
    GpsSimulator sim(transport);
    sim.injectMalformed(2);
    transport->open();
    for (int i = 0; i < 2; i++) {
        sim.generateOne();
        std::vector<uint8_t> bytes = transport->read(200, 20).value;
        std::string line(bytes.begin(), bytes.end());
        CHECK_CODE(parseNmea(line.substr(0, line.size() - 2), 0).status, ErrorCode::InvalidData);
    }
}

TEST(periodic_generation_starts_and_stops) {
    auto transport = std::make_shared<VirtualTransport>(1024);
    TemperatureSimulator sim(transport);
    CHECK(!sim.isRunning());
    sim.start(10);
    CHECK(sim.isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    sim.stop();
    CHECK(!sim.isRunning());
    unsigned long long count = sim.generatedCount();
    CHECK(count >= 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK_EQ(sim.generatedCount(), count);
}

TEST(disconnect_and_reconnect_control_the_transport) {
    auto transport = std::make_shared<VirtualTransport>();
    TemperatureSimulator sim(transport);
    sim.disconnect();
    CHECK(!transport->isConnected());
    sim.reconnect();
    CHECK(transport->isConnected());
}

TEST(virtual_device_simulator_loops_writes_back) {
    auto transport = std::make_shared<VirtualTransport>();
    VirtualDeviceSimulator sim(transport);
    transport->open();
    transport->write(toBytes("echo\n"));
    Result<std::vector<uint8_t> > echoed = transport->read(20, 50);
    CHECK_EQ(std::string(echoed.value.begin(), echoed.value.end()), std::string("echo\n"));
    Result<std::vector<uint8_t> > sample = transport->read(60, 50);
    std::string text(sample.value.begin(), sample.value.end());
    CHECK(text.find("SAMPLE seq=1 value=") == 0);
}

TEST_MAIN("unit: simulators")
