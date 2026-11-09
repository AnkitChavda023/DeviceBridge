#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include "Rigs.h"
#include "TestHelper.h"
#include "devices/Parsers.h"
#include "ioctl_commands.h"
#include "transport/CharacterDeviceTransport.h"
#include "transport/TtyTransport.h"

struct Fifo {
    std::string dir;
    std::string path;
    int peer;

    Fifo() : peer(-1) {
        char pattern[] = "/tmp/fwtestXXXXXX";
        dir = mkdtemp(pattern);
        path = dir + "/pipe";
        mkfifo(path.c_str(), 0600);
        peer = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    }
    ~Fifo() {
        if (peer >= 0) ::close(peer);
        unlink(path.c_str());
        rmdir(dir.c_str());
    }
    void send(const std::string& text) { ssize_t n = ::write(peer, text.data(), text.size()); (void)n; }
    std::string receive() {
        char buffer[8192];
        ssize_t n = ::read(peer, buffer, sizeof(buffer));
        return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string();
    }
};

struct Pty {
    int master;
    std::string slavePath;

    Pty() : master(-1) {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        grantpt(master);
        unlockpt(master);
        slavePath = ptsname(master);
    }
    ~Pty() {
        if (master >= 0) ::close(master);
    }
    void send(const std::string& text) { ssize_t n = ::write(master, text.data(), text.size()); (void)n; }
    std::string receive() {
        struct pollfd pfd;
        pfd.fd = master;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 1000) <= 0) return "";
        char buffer[512];
        ssize_t n = ::read(master, buffer, sizeof(buffer));
        return n > 0 ? std::string(buffer, static_cast<size_t>(n)) : std::string();
    }
};

TEST(missing_device_node_reports_disconnected) {
    CharacterDeviceTransport transport("/tmp/fw_no_such_device_node");
    CHECK_CODE(transport.open(), ErrorCode::Disconnected);
    CHECK(!transport.isOpen());
}

TEST(closed_transport_rejects_operations) {
    CharacterDeviceTransport transport("/tmp/fw_no_such_device_node");
    CHECK_CODE(transport.read(4, 10).status, ErrorCode::InvalidState);
    CHECK_CODE(transport.write(toBytes("x")).status, ErrorCode::InvalidState);
    CHECK_CODE(transport.flushInput(), ErrorCode::InvalidState);
    CHECK_CODE(transport.control(0, nullptr), ErrorCode::InvalidState);
    CHECK(!transport.waitReadable(10));
}

TEST(bytes_flow_both_ways_through_a_fifo) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path);
    CHECK_OK(transport.open());
    fifo.send("hello");
    Result<std::vector<uint8_t> > in = transport.read(16, 500);
    CHECK(in.ok());
    CHECK_EQ(std::string(in.value.begin(), in.value.end()), std::string("hello"));

    Result<size_t> out = transport.write(toBytes("abc"));
    CHECK(out.ok());
    CHECK_EQ(out.value, static_cast<size_t>(3));
    CHECK_EQ(fifo.receive(), std::string("abc"));
}

TEST(read_times_out_on_silent_device) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path);
    transport.open();
    long long start = steadyMs();
    CHECK_CODE(transport.read(4, 80).status, ErrorCode::Timeout);
    CHECK(steadyMs() - start >= 70);
    CHECK(!transport.waitReadable(10));
    fifo.send("x");
    CHECK(transport.waitReadable(200));
}

TEST(read_and_write_sizes_are_validated) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path);
    transport.open();
    CHECK_CODE(transport.read(0, 10).status, ErrorCode::InvalidArgument);
    CHECK_CODE(transport.read(kMaxTransferSize + 1, 10).status, ErrorCode::InvalidArgument);
    CHECK_CODE(transport.write(std::vector<uint8_t>()).status, ErrorCode::InvalidArgument);
    CHECK_CODE(transport.write(std::vector<uint8_t>(kMaxTransferSize + 1)).status, ErrorCode::InvalidArgument);
}

TEST(flush_discards_pending_input) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path);
    transport.open();
    fifo.send("stale data");
    CHECK_OK(transport.flushInput());
    CHECK_CODE(transport.read(16, 50).status, ErrorCode::Timeout);
}

TEST(maximum_size_write_is_completed) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path);
    transport.open();
    std::vector<uint8_t> big(kMaxTransferSize, 'z');
    Result<size_t> out = transport.write(big);
    CHECK(out.ok());
    CHECK_EQ(out.value, kMaxTransferSize);
    size_t total = 0;
    while (total < kMaxTransferSize) {
        std::string part = fifo.receive();
        if (part.empty()) break;
        total += part.size();
    }
    CHECK_EQ(total, kMaxTransferSize);
}

TEST(write_to_a_full_pipe_times_out) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path, 100);
    transport.open();
    std::vector<uint8_t> chunk(kMaxTransferSize, 'x');
    ErrorCode last = ErrorCode::Ok;
    for (int i = 0; i < 40 && last == ErrorCode::Ok; i++) {
        last = transport.write(chunk).status.code;
    }
    CHECK_EQ(last, ErrorCode::Timeout);
}

TEST(ioctl_on_a_plain_pipe_is_unsupported) {
    Fifo fifo;
    CharacterDeviceTransport transport(fifo.path);
    transport.open();
    struct vdev_status status;
    CHECK_CODE(transport.control(VDEV_GET_STATUS, &status), ErrorCode::Unsupported);
}

TEST(virtual_sensor_device_works_over_a_fifo) {
    Fifo fifo;
    std::shared_ptr<CharacterDeviceTransport> transport = std::make_shared<CharacterDeviceTransport>(fifo.path);
    VirtualSensorDevice device("vdev-fifo", "fifo device", transport, 300);
    device.onRegistered();
    CHECK_OK(device.open());
    CHECK_EQ(device.info().transport, TransportType::CharDevice);

    fifo.send("SAMPLE seq=5 value=23500\n");
    Result<Measurement> m = device.readMeasurement();
    CHECK(m.ok());
    CHECK_NEAR(m.value.field("seq"), 5, 0.001);
    CHECK_NEAR(m.value.field("value"), 23.5, 0.001);

    fifo.send("not a sample\n");
    CHECK_CODE(device.readMeasurement().status, ErrorCode::InvalidData);
    CHECK_EQ(device.state(), DeviceState::Open);

    CHECK(device.writeBytes(toBytes("ping\n")).ok());
    CHECK_EQ(fifo.receive(), std::string("ping\n"));

    CHECK_CODE(device.configure("sample_interval_ms", "50"), ErrorCode::Unsupported);
    CHECK_EQ(device.currentConfig()["sample_interval_ms"], std::string("200"));
    CHECK_OK(device.reset());
}

TEST(tty_transport_rejects_bad_baud_and_non_tty) {
    Fifo fifo;
    TtyTransport badBaud(fifo.path, 1234);
    CHECK_CODE(badBaud.open(), ErrorCode::InvalidArgument);
    TtyTransport notTty(fifo.path, 9600);
    CHECK_CODE(notTty.open(), ErrorCode::Unsupported);
    CHECK(!notTty.isOpen());
    CHECK(TtyTransport::isSupportedBaud(115200));
    CHECK(!TtyTransport::isSupportedBaud(1));
}

TEST(tty_transport_exchanges_bytes_over_a_pty) {
    Pty pty;
    TtyTransport transport(pty.slavePath, 9600);
    CHECK_OK(transport.open());
    CHECK_EQ(transport.type(), TransportType::UART);

    pty.send("ping");
    Result<std::vector<uint8_t> > in = transport.read(16, 500);
    CHECK(in.ok());
    CHECK_EQ(std::string(in.value.begin(), in.value.end()), std::string("ping"));

    CHECK(transport.write(toBytes("AT\r")).ok());
    CHECK_EQ(pty.receive(), std::string("AT\r"));
}

TEST(gps_device_reads_nmea_over_a_serial_port) {
    Pty pty;
    std::shared_ptr<TtyTransport> transport = std::make_shared<TtyTransport>(pty.slavePath, 9600);
    GpsDevice gps("gps-tty", "serial gps", transport, 500);
    gps.onRegistered();
    CHECK_OK(gps.open());

    pty.send(buildNmea("GPGSV,3,1,11,03,03,111,00,04,15,270,00"));
    pty.send(buildNmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
    Result<GpsMeasurement> m = gps.readPosition();
    CHECK(m.ok());
    CHECK(m.value.fixValid);
    CHECK_NEAR(m.value.latitude, 48.1173, 0.0001);

    pty.send("$GPGGA,garbage*00\r\n");
    CHECK_CODE(gps.readPosition().status, ErrorCode::InvalidData);
    CHECK_EQ(gps.state(), DeviceState::Open);
}

TEST(serial_port_hangup_puts_device_in_error_state) {
    std::shared_ptr<Pty> pty = std::make_shared<Pty>();
    std::shared_ptr<TtyTransport> transport = std::make_shared<TtyTransport>(pty->slavePath, 9600);
    GpsDevice gps("gps-tty", "serial gps", transport, 300);
    gps.onRegistered();
    gps.open();
    ::close(pty->master);
    pty->master = -1;
    Result<GpsMeasurement> m = gps.readPosition();
    CHECK(!m.ok());
    CHECK(m.status.code == ErrorCode::Disconnected || m.status.code == ErrorCode::IoError);
    CHECK_EQ(gps.state(), DeviceState::Error);
}

TEST_MAIN("integration: character device and tty transports")
