#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "TestHelper.h"
#include "ioctl_commands.h"
#include "util/Util.h"

static std::string devicePath = "/dev/vdev0";

static void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static int openDevice(int flags = O_RDWR) {
    return ::open(devicePath.c_str(), flags);
}

static int openCleanDevice(int flags = O_RDWR) {
    int fd = openDevice(O_RDWR);
    ioctl(fd, VDEV_RESET_DEVICE);
    if (flags != O_RDWR) {
        ::close(fd);
        fd = openDevice(flags);
    }
    return fd;
}

static std::string readAll(int fd) {
    std::string result;
    char buffer[512];
    while (true) {
        ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) break;
        result.append(buffer, static_cast<size_t>(n));
    }
    return result;
}

TEST(device_info_matches_the_header) {
    int fd = openCleanDevice();
    CHECK(fd >= 0);
    struct vdev_info info;
    CHECK_EQ(ioctl(fd, VDEV_GET_DEVICE_INFO, &info), 0);
    CHECK_EQ(std::string(info.name), std::string("vdev"));
    CHECK_EQ(info.version, static_cast<__u32>(VDEV_VERSION));
    CHECK_EQ(info.buffer_size, static_cast<__u32>(VDEV_BUFFER_SIZE));
    ::close(fd);
}

TEST(written_bytes_can_be_read_back) {
    int fd = openCleanDevice();
    CHECK_EQ(::write(fd, "hello", 5), static_cast<ssize_t>(5));
    struct vdev_status status;
    ioctl(fd, VDEV_GET_STATUS, &status);
    CHECK_EQ(status.buffered_bytes, 5u);
    char buffer[16];
    CHECK_EQ(::read(fd, buffer, sizeof(buffer)), static_cast<ssize_t>(5));
    CHECK_EQ(std::string(buffer, 5), std::string("hello"));
    ioctl(fd, VDEV_GET_STATUS, &status);
    CHECK_EQ(status.buffered_bytes, 0u);
    ::close(fd);
}

TEST(zero_length_operations_return_zero) {
    int fd = openCleanDevice();
    char buffer[4];
    CHECK_EQ(::read(fd, buffer, 0), static_cast<ssize_t>(0));
    CHECK_EQ(::write(fd, buffer, 0), static_cast<ssize_t>(0));
    ::close(fd);
}

TEST(non_blocking_read_on_empty_buffer_returns_eagain) {
    int fd = openCleanDevice();
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    char buffer[8];
    CHECK_EQ(::read(fd, buffer, sizeof(buffer)), static_cast<ssize_t>(-1));
    CHECK_EQ(errno, EAGAIN);
    ::close(fd);
}

TEST(blocking_read_waits_until_a_writer_arrives) {
    int fd = openCleanDevice();
    std::atomic<ssize_t> result(-5);
    std::atomic<long long> elapsed(0);
    std::thread reader([&] {
        long long start = steadyMs();
        char buffer[8];
        result = ::read(fd, buffer, sizeof(buffer));
        elapsed = steadyMs() - start;
    });
    sleepMs(150);
    int writer = openDevice();
    ::write(writer, "x", 1);
    reader.join();
    CHECK_EQ(result.load(), static_cast<ssize_t>(1));
    CHECK(elapsed.load() >= 120);
    ::close(writer);
    ::close(fd);
}

static void onSignal(int) {}

TEST(blocking_read_is_interrupted_by_a_signal) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = onSignal;
    sigaction(SIGUSR1, &action, nullptr);

    int fd = openCleanDevice();
    std::atomic<ssize_t> result(0);
    std::atomic<int> savedErrno(0);
    std::thread reader([&] {
        char buffer[8];
        result = ::read(fd, buffer, sizeof(buffer));
        savedErrno = errno;
    });
    sleepMs(150);
    pthread_kill(reader.native_handle(), SIGUSR1);
    reader.join();
    CHECK_EQ(result.load(), static_cast<ssize_t>(-1));
    CHECK_EQ(savedErrno.load(), EINTR);
    ::close(fd);
}

TEST(non_blocking_write_stops_when_the_buffer_is_full) {
    int fd = openCleanDevice();
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    std::string block(VDEV_BUFFER_SIZE, 'a');
    CHECK_EQ(::write(fd, block.data(), block.size()), static_cast<ssize_t>(VDEV_BUFFER_SIZE));
    CHECK_EQ(::write(fd, "b", 1), static_cast<ssize_t>(-1));
    CHECK_EQ(errno, EAGAIN);

    char buffer[10];
    CHECK_EQ(::read(fd, buffer, sizeof(buffer)), static_cast<ssize_t>(10));
    char more[20];
    memset(more, 'c', sizeof(more));
    CHECK_EQ(::write(fd, more, sizeof(more)), static_cast<ssize_t>(10));
    ::close(fd);
}

TEST(blocking_write_waits_for_a_reader) {
    int fd = openCleanDevice();
    std::string block(VDEV_BUFFER_SIZE, 'a');
    ::write(fd, block.data(), block.size());

    std::atomic<ssize_t> result(-5);
    std::thread writer([&] {
        std::string more(100, 'z');
        result = ::write(fd, more.data(), more.size());
    });
    sleepMs(150);
    CHECK_EQ(result.load(), static_cast<ssize_t>(-5));
    char buffer[256];
    ::read(fd, buffer, sizeof(buffer));
    writer.join();
    CHECK_EQ(result.load(), static_cast<ssize_t>(100));
    ::close(fd);
}

TEST(poll_reports_readable_and_writable_states) {
    int fd = openCleanDevice();
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN | POLLOUT;

    pfd.revents = 0;
    poll(&pfd, 1, 0);
    CHECK((pfd.revents & POLLOUT) != 0);
    CHECK((pfd.revents & POLLIN) == 0);

    ::write(fd, "x", 1);
    pfd.revents = 0;
    poll(&pfd, 1, 0);
    CHECK((pfd.revents & POLLIN) != 0);

    std::string block(VDEV_BUFFER_SIZE, 'a');
    int nonBlocking = openDevice(O_RDWR | O_NONBLOCK);
    ::write(nonBlocking, block.data(), block.size());
    pfd.revents = 0;
    poll(&pfd, 1, 0);
    CHECK((pfd.revents & POLLOUT) == 0);
    ::close(nonBlocking);
    ::close(fd);
}

TEST(poll_wakes_when_data_arrives) {
    int fd = openCleanDevice();
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    std::thread writer([] {
        sleepMs(100);
        int w = openDevice();
        ::write(w, "go", 2);
        ::close(w);
    });
    int ready = poll(&pfd, 1, 3000);
    writer.join();
    CHECK_EQ(ready, 1);
    CHECK((pfd.revents & POLLIN) != 0);
    ::close(fd);
}

TEST(sample_generator_produces_lines_until_stopped) {
    int fd = openCleanDevice();
    __u32 interval = 20;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), 0);
    sleepMs(400);

    struct vdev_status status;
    ioctl(fd, VDEV_GET_STATUS, &status);
    CHECK_EQ(status.sample_interval_ms, 20u);
    CHECK_EQ(status.generating, 1u);

    interval = 0;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), 0);
    sleepMs(100);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    std::string data = readAll(fd);
    CHECK(data.find("SAMPLE seq=1 value=") == 0);
    CHECK(data.find("SAMPLE seq=5 value=") != std::string::npos);

    struct vdev_stats stats;
    ioctl(fd, VDEV_GET_STATS, &stats);
    CHECK(stats.samples_generated >= 5);

    sleepMs(100);
    CHECK(readAll(fd).empty());
    ::close(fd);
}

TEST(invalid_intervals_are_rejected) {
    int fd = openCleanDevice();
    __u32 interval = 5;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), -1);
    CHECK_EQ(errno, EINVAL);
    interval = 70000;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), -1);
    CHECK_EQ(errno, EINVAL);
    interval = 10;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), 0);
    interval = 0;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), 0);
    ::close(fd);
}

TEST(read_only_open_cannot_change_the_device) {
    int fd = openCleanDevice(O_RDONLY);
    CHECK(fd >= 0);
    __u32 interval = 50;
    CHECK_EQ(ioctl(fd, VDEV_SET_SAMPLE_INTERVAL, &interval), -1);
    CHECK_EQ(errno, EPERM);
    CHECK_EQ(ioctl(fd, VDEV_RESET_DEVICE), -1);
    CHECK_EQ(errno, EPERM);
    CHECK_EQ(ioctl(fd, VDEV_CLEAR_BUFFER), -1);
    CHECK_EQ(errno, EPERM);
    struct vdev_status status;
    CHECK_EQ(ioctl(fd, VDEV_GET_STATUS, &status), 0);
    ::close(fd);
}

TEST(unknown_commands_are_rejected) {
    int fd = openCleanDevice();
    CHECK_EQ(ioctl(fd, _IO(VDEV_MAGIC, 99)), -1);
    CHECK_EQ(errno, ENOTTY);
    CHECK_EQ(ioctl(fd, _IO('Z', 1)), -1);
    CHECK_EQ(errno, ENOTTY);
    ::close(fd);
}

TEST(bad_user_pointers_return_efault) {
    int fd = openCleanDevice();
    CHECK_EQ(ioctl(fd, VDEV_GET_STATUS, reinterpret_cast<void*>(8)), -1);
    CHECK_EQ(errno, EFAULT);
    CHECK_EQ(::write(fd, reinterpret_cast<void*>(8), 16), static_cast<ssize_t>(-1));
    CHECK_EQ(errno, EFAULT);
    ::write(fd, "data", 4);
    CHECK_EQ(::read(fd, reinterpret_cast<void*>(8), 16), static_cast<ssize_t>(-1));
    CHECK_EQ(errno, EFAULT);
    ::close(fd);
}

TEST(clear_and_reset_empty_the_buffer) {
    int fd = openCleanDevice();
    ::write(fd, "abcdef", 6);
    CHECK_EQ(ioctl(fd, VDEV_CLEAR_BUFFER), 0);
    struct vdev_status status;
    ioctl(fd, VDEV_GET_STATUS, &status);
    CHECK_EQ(status.buffered_bytes, 0u);

    ::write(fd, "abcdef", 6);
    CHECK_EQ(ioctl(fd, VDEV_RESET_DEVICE), 0);
    ioctl(fd, VDEV_GET_STATUS, &status);
    CHECK_EQ(status.buffered_bytes, 0u);
    CHECK_EQ(status.sample_interval_ms, 0u);
    ::close(fd);
}

TEST(statistics_count_transferred_bytes) {
    int fd = openCleanDevice();
    ::write(fd, "0123456789", 10);
    char buffer[4];
    ::read(fd, buffer, sizeof(buffer));
    struct vdev_stats stats;
    CHECK_EQ(ioctl(fd, VDEV_GET_STATS, &stats), 0);
    CHECK_EQ(stats.bytes_written, 10ULL);
    CHECK_EQ(stats.bytes_read, 4ULL);
    CHECK(stats.opens >= 1);
    ::close(fd);
}

TEST(ring_buffer_wraps_around_correctly) {
    int fd = openCleanDevice();
    std::string expected;
    for (int round = 0; round < 40; round++) {
        std::string piece(300, static_cast<char>('a' + round % 26));
        ::write(fd, piece.data(), piece.size());
        std::string back(300, 0);
        ssize_t n = ::read(fd, &back[0], back.size());
        CHECK_EQ(n, static_cast<ssize_t>(300));
        CHECK(back == piece);
    }
    ::close(fd);
}

TEST(concurrent_writers_and_readers_lose_no_bytes) {
    int control = openCleanDevice();
    const int perWriter = 2000;
    std::atomic<int> totalRead(0);

    std::vector<std::thread> threads;
    for (int w = 0; w < 2; w++) {
        threads.push_back(std::thread([&] {
            int fd = openDevice();
            std::string data(100, 'w');
            for (int i = 0; i < perWriter / 100; i++) ::write(fd, data.data(), data.size());
            ::close(fd);
        }));
    }
    std::vector<std::thread> readers;
    for (int r = 0; r < 2; r++) {
        readers.push_back(std::thread([&] {
            int fd = openDevice(O_RDONLY | O_NONBLOCK);
            char buffer[64];
            while (true) {
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLIN;
                if (poll(&pfd, 1, 200) <= 0) break;
                ssize_t n = ::read(fd, buffer, sizeof(buffer));
                if (n > 0) totalRead += static_cast<int>(n);
            }
            ::close(fd);
        }));
    }
    for (size_t i = 0; i < threads.size(); i++) threads[i].join();
    for (size_t i = 0; i < readers.size(); i++) readers[i].join();
    CHECK_EQ(totalRead.load(), 2 * perWriter);
    ::close(control);
}

int main(int argc, char** argv) {
    if (argc > 1) devicePath = argv[1];
    if (access(devicePath.c_str(), F_OK) != 0) {
        std::cout << "SKIPPED: " << devicePath << " not found, load the driver first" << std::endl;
        return 77;
    }
    return runAllTests("driver");
}
