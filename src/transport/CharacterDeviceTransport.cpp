#include "transport/CharacterDeviceTransport.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

CharacterDeviceTransport::CharacterDeviceTransport(const std::string& path, int writeTimeoutMs)
    : fd_(-1), path_(path), writeTimeoutMs_(writeTimeoutMs) {}

CharacterDeviceTransport::~CharacterDeviceTransport() {
    close();
}

Status CharacterDeviceTransport::open() {
    if (fd_ >= 0) {
        return Status::success();
    }
    int fd = ::open(path_.c_str(), O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        int err = errno;
        return Status::failure(errorFromErrno(err), "open " + path_ + ": " + std::strerror(err));
    }
    fd_ = fd;
    return Status::success();
}

void CharacterDeviceTransport::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool CharacterDeviceTransport::isOpen() const {
    return fd_ >= 0;
}

Result<std::vector<uint8_t> > CharacterDeviceTransport::read(size_t maxBytes, int timeoutMs) {
    typedef Result<std::vector<uint8_t> > Out;
    if (fd_ < 0) {
        return Out::failure(ErrorCode::InvalidState, "transport is closed");
    }
    if (maxBytes == 0 || maxBytes > kMaxTransferSize) {
        return Out::failure(ErrorCode::InvalidArgument, "read size out of range");
    }

    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready;
    do {
        ready = ::poll(&pfd, 1, timeoutMs);
    } while (ready < 0 && errno == EINTR);

    if (ready < 0) {
        return Out::failure(errorFromErrno(errno), std::string("poll: ") + std::strerror(errno));
    }
    if (ready == 0) {
        return Out::failure(ErrorCode::Timeout, "read timed out");
    }
    if ((pfd.revents & POLLIN) == 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return Out::failure(ErrorCode::Disconnected, "device hung up");
    }

    std::vector<uint8_t> buffer(maxBytes);
    ssize_t count;
    do {
        count = ::read(fd_, buffer.data(), maxBytes);
    } while (count < 0 && errno == EINTR);

    if (count < 0) {
        int err = errno;
        return Out::failure(errorFromErrno(err), std::string("read: ") + std::strerror(err));
    }
    if (count == 0) {
        return Out::failure(ErrorCode::Disconnected, "end of stream");
    }
    buffer.resize(static_cast<size_t>(count));
    return Out::success(buffer);
}

Result<size_t> CharacterDeviceTransport::write(const std::vector<uint8_t>& data) {
    if (fd_ < 0) {
        return Result<size_t>::failure(ErrorCode::InvalidState, "transport is closed");
    }
    if (data.empty() || data.size() > kMaxTransferSize) {
        return Result<size_t>::failure(ErrorCode::InvalidArgument, "write size out of range");
    }

    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t count = ::write(fd_, data.data() + sent, data.size() - sent);
        if (count > 0) {
            sent += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && errno == EAGAIN) {
            struct pollfd pfd;
            pfd.fd = fd_;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int ready = ::poll(&pfd, 1, writeTimeoutMs_);
            if (ready == 0) {
                return Result<size_t>::failure(ErrorCode::Timeout, "write timed out");
            }
            if (ready < 0 && errno != EINTR) {
                return Result<size_t>::failure(errorFromErrno(errno), "poll failed");
            }
            continue;
        }
        int err = errno;
        return Result<size_t>::failure(errorFromErrno(err), std::string("write: ") + std::strerror(err));
    }
    return Result<size_t>::success(sent);
}

Status CharacterDeviceTransport::flushInput() {
    if (fd_ < 0) {
        return Status::failure(ErrorCode::InvalidState, "transport is closed");
    }
    char buffer[512];
    while (true) {
        ssize_t count = ::read(fd_, buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
    }
    return Status::success();
}

bool CharacterDeviceTransport::waitReadable(int timeoutMs) {
    if (fd_ < 0) {
        return false;
    }
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ready = ::poll(&pfd, 1, timeoutMs);
    return ready > 0 && (pfd.revents & POLLIN) != 0;
}

Status CharacterDeviceTransport::control(unsigned long request, void* argument) {
    if (fd_ < 0) {
        return Status::failure(ErrorCode::InvalidState, "transport is closed");
    }
    if (::ioctl(fd_, request, argument) < 0) {
        int err = errno;
        return Status::failure(errorFromErrno(err), std::string("ioctl: ") + std::strerror(err));
    }
    return Status::success();
}
