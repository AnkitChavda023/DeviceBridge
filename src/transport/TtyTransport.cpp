#include "transport/TtyTransport.h"
#include <cerrno>
#include <cstring>
#include <termios.h>

namespace {

bool baudConstant(int baud, speed_t& out) {
    switch (baud) {
        case 9600: out = B9600; return true;
        case 19200: out = B19200; return true;
        case 38400: out = B38400; return true;
        case 57600: out = B57600; return true;
        case 115200: out = B115200; return true;
    }
    return false;
}

}  // namespace

TtyTransport::TtyTransport(const std::string& path, int baud)
    : CharacterDeviceTransport(path), baud_(baud) {}

bool TtyTransport::isSupportedBaud(int baud) {
    speed_t unused;
    return baudConstant(baud, unused);
}

Status TtyTransport::open() {
    speed_t speed;
    if (!baudConstant(baud_, speed)) {
        return Status::failure(ErrorCode::InvalidArgument, "unsupported baud rate");
    }
    Status status = CharacterDeviceTransport::open();
    if (!status.ok()) {
        return status;
    }

    struct termios settings;
    if (tcgetattr(fd_, &settings) < 0) {
        int err = errno;
        close();
        return Status::failure(errorFromErrno(err), std::string("tcgetattr: ") + std::strerror(err));
    }
    cfmakeraw(&settings);
    cfsetispeed(&settings, speed);
    cfsetospeed(&settings, speed);
    settings.c_cflag |= (CLOCAL | CREAD);
    if (tcsetattr(fd_, TCSANOW, &settings) < 0) {
        int err = errno;
        close();
        return Status::failure(errorFromErrno(err), std::string("tcsetattr: ") + std::strerror(err));
    }
    return Status::success();
}
