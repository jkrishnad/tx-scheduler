#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "bench.hpp"
#include "socket.hpp"
#include "stats.hpp"

static int failures{};

static void check(bool ok, std::string_view name) {
    if (ok) {
        std::println("{} passed.", name);
    } else {
        ++failures;
        std::println("{} failed.", name);
    }
}

static void test_stats() {
    std::mt19937 rng{42};

    // Empty input
    {
        Stats s = compute_stats({});
        check(s.count == 0 && s.min == 0 && s.p50 == 0 && s.p99 == 0 &&
                  s.p999 == 0 && s.max == 0,
              "empty: every field is 0");
    }

    // One sample
    {
        Stats s = compute_stats({5});
        check(s.count == 1, "one: count is 1");
        check(s.min == 5 && s.p50 == 5 && s.p99 == 5 && s.p999 == 5 &&
                  s.max == 5,
              "one: every other field is 5");
    }

    // 7 samples: 10, 20, ..., 70
    {
        std::vector<double> v;
        for (int i = 1; i <= 7; ++i) {
            v.push_back(i * 10);
        }
        std::ranges::shuffle(v, rng);

        Stats s = compute_stats(v);
        check(s.count == 7, "seven: count is 7");
        check(s.min == 10, "seven: min is 10");
        check(s.max == 70, "seven: max is 70");
        check(s.p50 == 40, "seven: p50 is 40");
        check(s.p99 == 70, "seven: p99 is 70");
    }

    // 1000 samples: 1, 2, ..., 1000
    {
        std::vector<double> v;
        for (int i = 1; i <= 1000; ++i) {
            v.push_back(i);
        }
        std::ranges::shuffle(v, rng);

        Stats s = compute_stats(v);
        check(s.count == 1000, "thousand: count is 1000");
        check(s.min == 1, "thousand: min is 1");
        check(s.max == 1000, "thousand: max is 1000");
        check(s.p50 == 500, "thousand: p50 is 500");
        check(s.p99 == 990, "thousand: p99 is 990");
        check(s.p999 == 999, "thousand: p999 is 999");

        print_stats("thousand", s);
    }
}

static void test_bench() {
    // Returns one sample per run
    {
        std::vector<double> samples = time_runs([] {}, 100);
        check(samples.size() == 100, "bench: 100 runs give 100 samples");
        check(std::ranges::all_of(samples, [](double ns) { return ns >= 0; }),
              "bench: every sample is >= 0");
    }

    // Calls the function exactly `runs` times
    {
        int calls{0};
        time_runs([&calls] { ++calls; }, 50);
        check(calls == 50, "bench: function called 50 times");
    }

    // Zero runs gives no samples
    {
        std::vector<double> samples = time_runs([] {}, 0);
        check(samples.empty(), "bench: 0 runs give no samples");
    }

    // Samples are in nanoseconds: a 1 ms sleep takes at least 1,000,000 ns
    {
        std::vector<double> samples = time_runs(
            [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); },
            3);
        check(std::ranges::all_of(samples,
                                  [](double ns) { return ns >= 1'000'000; }),
              "bench: 1 ms sleep measures >= 1,000,000 ns");
    }
}

// Test-only server side of a Unix socket: listens on a fresh path in the temp
// directory, accepts one client, and cleans up after itself.
struct TestServer {
    std::string path;
    int listen_fd{-1};
    int conn_fd{-1};

    TestServer() {
        path =
            (std::filesystem::temp_directory_path() / "tx-scheduler-test.sock")
                .string();
        std::filesystem::remove(path);

        listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        ::bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        ::listen(listen_fd, 1);
    }

    // Call after the client connected. Waits at most 1 s for it, and every
    // later read waits at most 1 s, so a broken client fails a test instead
    // of hanging the run.
    void accept_client() {
        pollfd p{listen_fd, POLLIN, 0};
        if (::poll(&p, 1, 1000) <= 0) {
            return;
        }
        conn_fd = ::accept(listen_fd, nullptr, nullptr);
        timeval timeout{1, 0};
        ::setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout));
    }

    // Reads exactly n bytes from the client.
    std::vector<std::byte> read_exact(std::size_t n) {
        std::vector<std::byte> out(n);
        std::size_t got{0};
        while (got < n) {
            ssize_t r = ::read(conn_fd, out.data() + got, n - got);
            if (r <= 0) {
                out.resize(got);
                break;
            }
            got += static_cast<std::size_t>(r);
        }
        return out;
    }

    // Sends bytes plus file descriptors (SCM_RIGHTS), the way Agave does.
    void send_with_fds(std::span<const std::byte> bytes,
                       std::span<const int> fds) {
        iovec iov{const_cast<std::byte *>(bytes.data()), bytes.size()};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        std::vector<std::byte> control(CMSG_SPACE(sizeof(int) * fds.size()));
        if (!fds.empty()) {
            msg.msg_control = control.data();
            msg.msg_controllen = static_cast<socklen_t>(control.size());
            cmsghdr *c = CMSG_FIRSTHDR(&msg);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            c->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
            std::memcpy(CMSG_DATA(c), fds.data(), sizeof(int) * fds.size());
        }
        ::sendmsg(conn_fd, &msg, 0);
    }

    ~TestServer() {
        if (conn_fd >= 0) {
            ::close(conn_fd);
        }
        ::close(listen_fd);
        std::filesystem::remove(path);
    }
};

static std::vector<std::byte> bytes_of(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

static void test_socket() {
    // Connecting to a path nobody listens on throws
    {
        bool threw{false};
        try {
            UnixSocket s{"/tmp/tx-scheduler-nobody-here.sock"};
        } catch (const std::system_error &) {
            threw = true;
        }
        check(threw, "socket: missing path throws std::system_error");
    }

    // send_all delivers every byte, in order (the 1024-byte logon)
    {
        TestServer server;
        UnixSocket s{server.path};
        server.accept_client();

        std::vector<std::byte> logon(1024);
        for (std::size_t i = 0; i < logon.size(); ++i) {
            logon[i] = static_cast<std::byte>(i % 256);
        }
        s.send_all(logon);

        check(server.read_exact(1024) == logon,
              "socket: send_all delivers 1024 bytes");
    }

    // A reply without fds: bytes are exactly what was sent, fds is empty
    // (the shape of Agave's rejection: 0x02, length, reason)
    {
        TestServer server;
        UnixSocket s{server.path};
        server.accept_client();

        std::vector<std::byte> reject = bytes_of({0x02, 3, 'b', 'a', 'd'});
        server.send_with_fds(reject, {});

        Reply r = s.recv_with_fds();
        check(r.bytes == reject,
              "socket: reply bytes match, sized to what arrived");
        check(r.fds.empty(), "socket: no fds when none were sent");
    }

    // A reply with fds: they arrive in order and work
    // (the shape of Agave's success: 0x01 + fds)
    {
        TestServer server;
        UnixSocket s{server.path};
        server.accept_client();

        int pipe_fds[2];
        ::pipe(pipe_fds); // [0] read end, [1] write end
        std::array<int, 2> sent{pipe_fds[0], pipe_fds[1]};
        server.send_with_fds(bytes_of({0x01}), sent);

        Reply r = s.recv_with_fds();
        check(r.bytes == bytes_of({0x01}), "socket: success byte arrives");
        check(r.fds.size() == 2, "socket: 2 fds arrive");

        if (r.fds.size() == 2) {
            // Write into the received write end, read from the received read
            // end. Only works if both fds are real and in the order they were
            // sent.
            char out{'x'};
            char in{};
            ::write(r.fds[1], &out, 1);
            ::read(r.fds[0], &in, 1);
            check(in == 'x', "socket: received fds work, in order");
            ::close(r.fds[0]);
            ::close(r.fds[1]);
        }
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
    }

    // Move: the new socket works after the moved-from one is destroyed
    {
        TestServer server;
        std::optional<UnixSocket> a;
        a.emplace(server.path);
        server.accept_client();

        UnixSocket b{std::move(*a)};
        a.reset(); // destroys the moved-from socket: must not close b's fd

        std::vector<std::byte> msg = bytes_of({1, 2, 3, 4});
        b.send_all(msg);
        check(server.read_exact(4) == msg,
              "socket: moved-to socket still sends");
    }
}

int main() {
    test_stats();
    test_bench();
    test_socket();

    std::println("\n{} failures.", failures);
    return failures == 0 ? 0 : 1;
}
