#include "socket.hpp"
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

UnixSocket::UnixSocket(const std::string &path) {
    // create the socket
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    // connect to the socket
    if (connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        int err = errno;
        close(fd_);
        fd_ = -1;
        throw std::system_error(err, std::generic_category(), "connect");
    }
}

UnixSocket::~UnixSocket() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

UnixSocket::UnixSocket(UnixSocket &&other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

void UnixSocket::send_all(std::span<const std::byte> bytes) {
    size_t sent{0};

    while (sent < bytes.size()) {
        ssize_t n = write(fd_, bytes.data() + sent, bytes.size() - sent);

        if (n <= 0) {
            throw std::system_error(errno, std::generic_category(), "write");
        }
        sent += static_cast<size_t>(n);
    }
}

Reply UnixSocket::recv_with_fds() {

    Reply r;
    r.bytes.resize(1024);
    iovec ivo{r.bytes.data(), r.bytes.size()};

    alignas(cmsghdr) std::byte ctl[CMSG_SPACE(sizeof(int) * 133)];

    msghdr msg{};
    msg.msg_iov = &ivo;
    msg.msg_iovlen = 1;
    msg.msg_control = ctl;
    msg.msg_controllen = sizeof(ctl);

    ssize_t n = ::recvmsg(fd_, &msg, 0);
    if (n < 0) {
        throw std::system_error(errno, std::generic_category(), "recvmsg");
    }
    r.bytes.resize(static_cast<size_t>(n));

    for (cmsghdr *c = CMSG_FIRSTHDR(&msg); c != nullptr;
         c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            std::size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            r.fds.resize(count);
            std::memcpy(r.fds.data(), CMSG_DATA(c), count * sizeof(int));
        }
    }
    return r;
}
