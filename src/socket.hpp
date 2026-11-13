#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Everything one recvmsg returned: the data bytes and any file descriptors.
struct Reply {
    std::vector<std::byte> bytes;
    std::vector<int> fds;
};

// A connected Unix stream socket. Owns its fd and closes it exactly once.
class UnixSocket {
  public:
    // Constructor: runs on `UnixSocket s{path};`. Opens the socket and
    // connects; throws on failure.
    explicit UnixSocket(const std::string &path);
    // Destructor: runs by itself when the object goes out of scope.
    // Closes the fd if still owned.
    ~UnixSocket();

    // Copy constructor, deleted: `UnixSocket b = a;` won't compile
    UnixSocket(const UnixSocket &) = delete;

    // Copy assignment, deleted: `b = a;` on an existing b won't compile,
    UnixSocket &operator=(const UnixSocket &) = delete;

    // Move constructor: runs on `UnixSocket b{std::move(a)};`.
    UnixSocket(UnixSocket &&other) noexcept;

    // Writes every byte, looping until all are sent.
    void send_all(std::span<const std::byte> bytes);
    // Reads one message, plus any fds passed with it (SCM_RIGHTS).
    Reply recv_with_fds();

  private:
    int fd_{-1}; // -1 = owns nothing
};
