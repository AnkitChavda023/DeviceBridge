#include <atomic>
#include <chrono>
#include <thread>
#include "TestHelper.h"
#include "transport/VirtualTransport.h"
#include "util/Util.h"

TEST(read_requires_open_transport) {
    VirtualTransport transport;
    CHECK_CODE(transport.read(4, 10).status, ErrorCode::InvalidState);
    CHECK_CODE(transport.write(toBytes("x")).status, ErrorCode::InvalidState);
    CHECK_OK(transport.open());
    CHECK(transport.isOpen());
    transport.close();
    CHECK(!transport.isOpen());
}

TEST(fed_bytes_are_read_back) {
    VirtualTransport transport;
    transport.open();
    transport.feed(toBytes("abcdef"));
    Result<std::vector<uint8_t> > first = transport.read(4, 50);
    CHECK(first.ok());
    CHECK_EQ(first.value.size(), static_cast<size_t>(4));
    Result<std::vector<uint8_t> > second = transport.read(4, 50);
    CHECK_EQ(second.value.size(), static_cast<size_t>(2));
}

TEST(read_times_out_when_empty) {
    VirtualTransport transport;
    transport.open();
    long long start = steadyMs();
    CHECK_CODE(transport.read(4, 80).status, ErrorCode::Timeout);
    CHECK(steadyMs() - start >= 70);
}

TEST(blocked_reader_wakes_when_data_arrives) {
    VirtualTransport transport;
    transport.open();
    std::thread feeder([&transport] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        transport.feed(toBytes("z"));
    });
    Result<std::vector<uint8_t> > result = transport.read(4, 2000);
    feeder.join();
    CHECK(result.ok());
}

TEST(full_buffer_drops_oldest_bytes) {
    VirtualTransport transport(4);
    transport.open();
    transport.feed(toBytes("abcdef"));
    CHECK_EQ(transport.pendingBytes(), static_cast<size_t>(4));
    CHECK_EQ(transport.overflowBytes(), 2ULL);
    Result<std::vector<uint8_t> > result = transport.read(10, 10);
    CHECK_EQ(std::string(result.value.begin(), result.value.end()), std::string("cdef"));
}

TEST(disconnect_fails_reads_and_wakes_waiters) {
    VirtualTransport transport;
    transport.open();
    std::thread cutter([&transport] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        transport.setConnected(false);
    });
    Result<std::vector<uint8_t> > result = transport.read(4, 2000);
    cutter.join();
    CHECK_CODE(result.status, ErrorCode::Disconnected);
    CHECK_CODE(transport.write(toBytes("x")).status, ErrorCode::Disconnected);
    transport.close();
    CHECK_CODE(transport.open(), ErrorCode::Disconnected);
    transport.setConnected(true);
    CHECK_OK(transport.open());
}

TEST(write_validates_size_and_records_bytes) {
    VirtualTransport transport;
    transport.open();
    CHECK_CODE(transport.write(std::vector<uint8_t>()).status, ErrorCode::InvalidArgument);
    CHECK_CODE(transport.write(std::vector<uint8_t>(kMaxTransferSize + 1)).status, ErrorCode::InvalidArgument);
    CHECK_CODE(transport.read(0, 10).status, ErrorCode::InvalidArgument);
    Result<size_t> written = transport.write(toBytes("hello"));
    CHECK(written.ok());
    CHECK_EQ(written.value, static_cast<size_t>(5));
    CHECK_EQ(toHex(transport.takeWritten()), toHex(toBytes("hello")));
    CHECK(transport.takeWritten().empty());
}

TEST(partial_writes_are_completed_in_chunks) {
    VirtualTransport transport;
    transport.open();
    transport.setWriteChunkSize(3);
    Result<size_t> written = transport.write(toBytes("0123456789"));
    CHECK(written.ok());
    CHECK_EQ(written.value, static_cast<size_t>(10));
    CHECK_EQ(transport.chunkedWrites(), 3ULL);
    CHECK_EQ(transport.takeWritten().size(), static_cast<size_t>(10));
}

TEST(injected_write_failures_are_counted_down) {
    VirtualTransport transport;
    transport.open();
    transport.setFailNextWrites(2);
    CHECK_CODE(transport.write(toBytes("a")).status, ErrorCode::IoError);
    CHECK_CODE(transport.write(toBytes("a")).status, ErrorCode::IoError);
    CHECK(transport.write(toBytes("a")).ok());
}

TEST(read_delay_longer_than_timeout_times_out) {
    VirtualTransport transport;
    transport.open();
    transport.feed(toBytes("a"));
    transport.setReadDelayMs(300);
    CHECK_CODE(transport.read(1, 50).status, ErrorCode::Timeout);
    transport.setReadDelayMs(20);
    CHECK(transport.read(1, 500).ok());
}

TEST(data_provider_fills_empty_buffer) {
    VirtualTransport transport;
    transport.open();
    VirtualTransport* raw = &transport;
    transport.setDataProvider([raw] { raw->feed(toBytes("on-demand")); });
    Result<std::vector<uint8_t> > result = transport.read(20, 100);
    CHECK(result.ok());
    CHECK_EQ(std::string(result.value.begin(), result.value.end()), std::string("on-demand"));
}

TEST(write_handler_sees_written_bytes) {
    VirtualTransport transport;
    transport.open();
    std::string seen;
    transport.setWriteHandler([&seen](const std::vector<uint8_t>& data) { seen.assign(data.begin(), data.end()); });
    transport.write(toBytes("ping"));
    CHECK_EQ(seen, std::string("ping"));
}

TEST(flush_and_wait_readable) {
    VirtualTransport transport;
    transport.open();
    CHECK(!transport.waitReadable(20));
    transport.feed(toBytes("a"));
    CHECK(transport.waitReadable(20));
    transport.flushInput();
    CHECK_EQ(transport.pendingBytes(), static_cast<size_t>(0));
}

TEST_MAIN("unit: virtual transport")
