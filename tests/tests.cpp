#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "headers/agave_link.hpp"
#include "headers/base58.hpp"
#include "headers/bench.hpp"
#include "headers/bindings.hpp"
#include "headers/rts_alloc.hpp"
#include "headers/shm.hpp"
#include "headers/socket.hpp"
#include "headers/spsc.hpp"
#include "headers/stats.hpp"

// Layout checks: every size and field offset must match Agave's Rust structs
// (agave-scheduler-bindings 6.0.0, agave-scheduler-handshake 7.0.0) byte for
// byte. A mismatch fails the build.

static_assert(sizeof(ProtocolVersions) == 32);
static_assert(offsetof(ProtocolVersions, handshake) == 0);
static_assert(offsetof(ProtocolVersions, scheduler_bindings) == 8);
static_assert(offsetof(ProtocolVersions, shaq) == 16);
static_assert(offsetof(ProtocolVersions, rts_alloc) == 24);

static_assert(sizeof(ClientLogon) == 88);
static_assert(offsetof(ClientLogon, worker_count) == 0);
static_assert(offsetof(ClientLogon, check_worker_count) == 8);
static_assert(offsetof(ClientLogon, allocator_size) == 16);
static_assert(offsetof(ClientLogon, allocator_handles) == 24);
static_assert(offsetof(ClientLogon, tpu_to_pack_capacity) == 32);
static_assert(offsetof(ClientLogon, progress_tracker_capacity) == 40);
static_assert(offsetof(ClientLogon, pack_to_worker_capacity) == 48);
static_assert(offsetof(ClientLogon, worker_to_pack_capacity) == 56);
static_assert(offsetof(ClientLogon, pack_to_check_worker_capacity) == 64);
static_assert(offsetof(ClientLogon, check_worker_to_pack_capacity) == 72);
static_assert(offsetof(ClientLogon, flags) == 80);

static_assert(sizeof(SharableTransactionRegion) == 16);
static_assert(offsetof(SharableTransactionRegion, offset) == 0);
static_assert(offsetof(SharableTransactionRegion, length) == 8);

static_assert(sizeof(SharableTransactionBatchRegion) == 16);
static_assert(offsetof(SharableTransactionBatchRegion, num_transactions) == 0);
static_assert(offsetof(SharableTransactionBatchRegion, transactions_offset) ==
              8);

static_assert(sizeof(ExecutionResponseRegion) == 16);
static_assert(offsetof(ExecutionResponseRegion, num_transaction_responses) ==
              0);
static_assert(offsetof(ExecutionResponseRegion, transaction_responses_offset) ==
              8);

static_assert(sizeof(TpuToPackMessage) == 40);
static_assert(offsetof(TpuToPackMessage, transaction) == 0);
static_assert(offsetof(TpuToPackMessage, flags) == 16);
static_assert(offsetof(TpuToPackMessage, src_addr) == 17);

static_assert(sizeof(ProgressMessage) == 96);
static_assert(offsetof(ProgressMessage, leader_state) == 0);
static_assert(offsetof(ProgressMessage, current_slot_progress) == 1);
static_assert(offsetof(ProgressMessage, epoch) == 8);
static_assert(offsetof(ProgressMessage, current_slot) == 16);
static_assert(offsetof(ProgressMessage, next_leader_slot) == 24);
static_assert(offsetof(ProgressMessage, leader_range_end) == 32);
static_assert(offsetof(ProgressMessage, remaining_cost_units) == 40);
static_assert(offsetof(ProgressMessage,
                       remaining_allocated_accounts_data_size) == 48);
static_assert(offsetof(ProgressMessage, latest_blockhash) == 56);
static_assert(offsetof(ProgressMessage, target_bank_time_ms) == 88);

static_assert(sizeof(PackToExecutionWorkerMessage) == 32);
static_assert(offsetof(PackToExecutionWorkerMessage, flags) == 0);
static_assert(offsetof(PackToExecutionWorkerMessage, max_working_slot) == 8);
static_assert(offsetof(PackToExecutionWorkerMessage, batch) == 16);

static_assert(sizeof(ExecutionWorkerToPackMessage) == 40);
static_assert(offsetof(ExecutionWorkerToPackMessage, batch) == 0);
static_assert(offsetof(ExecutionWorkerToPackMessage, processed_code) == 16);
static_assert(offsetof(ExecutionWorkerToPackMessage, responses) == 24);

static_assert(sizeof(ExecutionResponse) == 32);
static_assert(offsetof(ExecutionResponse, execution_slot) == 0);
static_assert(offsetof(ExecutionResponse, not_included_reason) == 8);
static_assert(offsetof(ExecutionResponse, cost_units) == 16);
static_assert(offsetof(ExecutionResponse, fee_payer_balance) == 24);

// Field widths: a wider type can still land on the right offset (it fills the
// padding), but then it reads padding bytes as part of the value.
static_assert(sizeof(SharableTransactionRegion::length) == 4);
static_assert(sizeof(SharableTransactionBatchRegion::num_transactions) == 1);
static_assert(sizeof(ExecutionResponseRegion::num_transaction_responses) == 1);
static_assert(sizeof(TpuToPackMessage::flags) == 1);
static_assert(sizeof(TpuToPackMessage::src_addr) == 16);
static_assert(sizeof(ProgressMessage::leader_state) == 1);
static_assert(sizeof(ProgressMessage::current_slot_progress) == 1);
static_assert(sizeof(ProgressMessage::latest_blockhash) == 32);
static_assert(sizeof(ProgressMessage::target_bank_time_ms) == 2);
static_assert(sizeof(PackToExecutionWorkerMessage::flags) == 2);
static_assert(sizeof(ExecutionWorkerToPackMessage::processed_code) == 1);
static_assert(sizeof(ExecutionResponse::not_included_reason) == 1);
static_assert(alignof(TpuToPackMessage) == 8);

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

// A temp file of a given size: stands in for a memory block Agave created.
struct TestFile {
    std::string path{
        (std::filesystem::temp_directory_path() / "tx-scheduler-test.shm")
            .string()};

    explicit TestFile(off_t size) {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        ::ftruncate(fd, size);
        ::close(fd);
    }
    ~TestFile() { ::unlink(path.c_str()); }

    int open_fd() const { return ::open(path.c_str(), O_RDWR); }
};

static bool fd_is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

static void test_shm() {
    // Size comes from the fd, and a new block reads as zeros
    {
        TestFile file{4096};
        SharedRegion region{file.open_fd()};
        check(region.size() == 4096, "shm: size read from the fd");
        bool zeros = std::all_of(region.data(), region.data() + region.size(),
                                 [](std::byte b) { return b == std::byte{0}; });
        check(zeros, "shm: new block reads as zeros");
    }

    // Writes through data() reach the block
    {
        TestFile file{4096};
        SharedRegion region{file.open_fd()};
        region.data()[0] = std::byte{'j'};
        region.data()[1] = std::byte{'k'};

        char buf[2]{};
        int other = file.open_fd();
        ::pread(other, buf, 2, 0);
        ::close(other);
        check(buf[0] == 'j' && buf[1] == 'k',
              "shm: writes through data() reach the block");
    }

    // Writes from another fd show up in data()
    {
        TestFile file{4096};
        SharedRegion region{file.open_fd()};

        int other = file.open_fd();
        ::pwrite(other, "hi", 2, 100);
        ::close(other);
        check(region.data()[100] == std::byte{'h'} &&
                  region.data()[101] == std::byte{'i'},
              "shm: writes from another fd show up in data()");
    }

    // Destructor closes the fd
    {
        TestFile file{4096};
        int fd = file.open_fd();
        {
            SharedRegion region{fd};
        }
        check(!fd_is_open(fd), "shm: destructor closes the fd");
    }

    // A bad fd throws
    {
        bool threw = false;
        try {
            SharedRegion region{-1};
        } catch (const std::system_error &) {
            threw = true;
        }
        check(threw, "shm: bad fd throws");
    }

    // A failed map throws and still closes the fd (an empty block can't map)
    {
        TestFile file{0};
        int fd = file.open_fd();
        bool threw = false;
        try {
            SharedRegion region{fd};
        } catch (const std::system_error &) {
            threw = true;
        }
        check(threw && !fd_is_open(fd),
              "shm: failed map throws and closes the fd");
    }

    // Move: the new region owns the mapping, the old one owns nothing
    {
        TestFile file{4096};
        int fd = file.open_fd();
        std::optional<SharedRegion> a;
        a.emplace(fd);
        a->data()[0] = std::byte{'m'};

        SharedRegion b{std::move(*a)};
        check(a->data() == nullptr && a->size() == 0,
              "shm: moved-from region owns nothing");

        a.reset(); // destroys the moved-from region: must not unmap b's memory
        check(fd_is_open(fd) && b.size() == 4096 &&
                  b.data()[0] == std::byte{'m'},
              "shm: moved-to region still works");
    }
}

// A test message: 16 bytes, so 4 slots fit after the 256-byte queue header.
struct TestMsg {
    std::uint64_t id;
    std::uint64_t value;
};

// Plays Agave's side of a shaq SPSC queue. It maps the same file as the
// consumer, the way two processes map the same block, and lays out the
// header the way shaq 4.4.0 does: magic @0, version @8, buffer_mask @12,
// write cursor @64, read cursor @128, slots from @256.
struct FakeQueue {
    static constexpr std::uint64_t magic = 0x7368617173707363; // "shaqspsc"
    static constexpr std::uint32_t version = 3 << 16;
    static constexpr std::size_t slots_offset = 256;

    TestFile file;
    SharedRegion region;
    std::uint32_t mask;

    // capacity must be a power of two
    explicit FakeQueue(std::uint32_t capacity)
        : file{static_cast<off_t>(slots_offset + capacity * sizeof(TestMsg))},
          region{file.open_fd()}, mask{capacity - 1} {
        set_header(magic, version, mask);
    }

    void set_header(std::uint64_t m, std::uint32_t v, std::uint32_t mk) {
        std::memcpy(region.data() + 0, &m, sizeof(m));
        std::memcpy(region.data() + 8, &v, sizeof(v));
        std::memcpy(region.data() + 12, &mk, sizeof(mk));
    }

    std::atomic_ref<std::uint64_t> cursor(std::size_t offset) {
        return std::atomic_ref<std::uint64_t>{
            *reinterpret_cast<std::uint64_t *>(region.data() + offset)};
    }
    std::atomic_ref<std::uint64_t> write() { return cursor(64); }
    std::atomic_ref<std::uint64_t> read() { return cursor(128); }

    // Puts msg in its slot without announcing it.
    void fill_slot(std::uint64_t position, TestMsg msg) {
        std::memcpy(region.data() + slots_offset +
                        (position & mask) * sizeof(TestMsg),
                    &msg, sizeof(msg));
    }

    // Data first, then the write cursor.
    void push(TestMsg msg) {
        std::uint64_t w = write().load(std::memory_order_relaxed);
        fill_slot(w, msg);
        write().store(w + 1, std::memory_order_release);
    }

    // Starts both cursors at n, as if n messages already went through.
    void skip_to(std::uint64_t n) {
        write().store(n);
        read().store(n);
    }

    // The message in a slot, whether or not it was announced.
    TestMsg slot(std::uint64_t position) {
        TestMsg msg{};
        std::memcpy(&msg,
                    region.data() + slots_offset +
                        (position & mask) * sizeof(TestMsg),
                    sizeof(msg));
        return msg;
    }

    // Reads like Agave's consumer: write cursor first, then the slot, then
    // the read cursor.
    std::optional<TestMsg> pop() {
        std::uint64_t r = read().load(std::memory_order_relaxed);
        if (write().load(std::memory_order_acquire) == r) {
            return std::nullopt;
        }
        TestMsg msg = slot(r);
        read().store(r + 1, std::memory_order_release);
        return msg;
    }

    // A second mapping of the same file, for our side of the queue.
    SharedRegion consumer_region() const {
        return SharedRegion{file.open_fd()};
    }
};

static bool throws_on_join(FakeQueue &p) {
    try {
        SpscConsumer<TestMsg> q{p.consumer_region()};
    } catch (const std::runtime_error &) {
        return true;
    }
    return false;
}

static void test_spsc() {
    // Joining checks the header the way shaq does
    {
        FakeQueue p{4};
        p.set_header(0, FakeQueue::version, p.mask);
        check(throws_on_join(p), "spsc: wrong magic throws");
    }
    {
        FakeQueue p{4};
        p.set_header(FakeQueue::magic, 2 << 16, p.mask);
        check(throws_on_join(p), "spsc: wrong version throws");
    }
    {
        FakeQueue p{4};
        p.set_header(FakeQueue::magic, FakeQueue::version, 7);
        check(throws_on_join(p), "spsc: mask that doesn't fit the file throws");
    }

    // Capacity comes from the header
    {
        FakeQueue p{4};
        SpscConsumer<TestMsg> q{p.consumer_region()};
        check(q.capacity() == 4, "spsc: capacity is 4");
    }

    // Empty: nothing to read, read cursor stays put
    {
        FakeQueue p{4};
        SpscConsumer<TestMsg> q{p.consumer_region()};
        check(!q.try_read().has_value(), "spsc: empty queue reads nothing");
        check(p.read().load() == 0, "spsc: empty read leaves cursor at 0");
    }

    // One message: read it, cursor moves, then empty again
    {
        FakeQueue p{4};
        SpscConsumer<TestMsg> q{p.consumer_region()};
        p.push({1, 100});

        std::optional<TestMsg> m = q.try_read();
        check(m && m->id == 1 && m->value == 100,
              "spsc: reads the pushed message");
        check(p.read().load() == 1, "spsc: read cursor moves to 1");
        check(!q.try_read().has_value(), "spsc: empty again after reading");
    }

    // A filled slot that write hasn't announced yet is not readable
    {
        FakeQueue p{4};
        SpscConsumer<TestMsg> q{p.consumer_region()};
        p.fill_slot(0, {9, 9});
        check(!q.try_read().has_value(),
              "spsc: slot data without write bump is not read");
    }

    // Cursors never reset: positions 6, 7, 8 land in slots 2, 3, 0
    {
        FakeQueue p{4};
        p.skip_to(6);
        SpscConsumer<TestMsg> q{p.consumer_region()};
        p.push({6, 0});
        p.push({7, 0});
        p.push({8, 0});

        std::optional<TestMsg> a = q.try_read();
        std::optional<TestMsg> b = q.try_read();
        std::optional<TestMsg> c = q.try_read();
        check(a && b && c && a->id == 6 && b->id == 7 && c->id == 8,
              "spsc: wraps around the ring in order");
        check(p.read().load() == 9, "spsc: read cursor reaches 9");
    }

    // Live producer thread: 100,000 messages through 4 slots, none lost,
    // none out of order
    {
        constexpr std::uint64_t total = 100'000;
        FakeQueue p{4};
        SpscConsumer<TestMsg> q{p.consumer_region()};

        // Both sides give up after 5 s, so a broken consumer fails the test
        // instead of hanging the run.
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);

        std::thread producer{[&p, deadline] {
            for (std::uint64_t i = 0; i < total; ++i) {
                // Full: wait for the consumer to free a slot
                while (p.write().load(std::memory_order_relaxed) -
                           p.read().load(std::memory_order_acquire) >
                       p.mask) {
                    if (std::chrono::steady_clock::now() > deadline) {
                        return;
                    }
                }
                p.push({i, i * 2});
            }
        }};

        std::uint64_t got{0};
        bool in_order{true};
        while (got < total && std::chrono::steady_clock::now() < deadline) {
            if (std::optional<TestMsg> m = q.try_read()) {
                in_order = in_order && m->id == got && m->value == got * 2;
                ++got;
            }
        }
        producer.join();
        check(got == total && in_order,
              "spsc: 100,000 messages from a live producer, in order");
    }
}

// Plays Agave for one handshake on a background thread: accepts the client,
// reads its 1024-byte logon, then answers with `reply` and `fds`.
struct FakeAgave {
    TestServer server;
    std::vector<std::byte> logon;
    std::thread thread;

    FakeAgave(std::vector<std::byte> reply, std::vector<int> fds)
        : thread{[this, reply, fds] {
              server.accept_client();
              logon = server.read_exact(1024);
              server.send_with_fds(reply, fds);
          }} {}

    ~FakeAgave() {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Waits for the handshake to finish, so `logon` is safe to read.
    void finish() { thread.join(); }
};

static std::vector<std::byte> rejection(std::string_view reason) {
    std::vector<std::byte> out{std::byte{0x02},
                               static_cast<std::byte>(reason.size())};
    for (char c : reason) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

static ClientLogon test_logon() {
    ClientLogon logon{};
    logon.worker_count = 1;
    logon.check_worker_count = 1;
    logon.allocator_size = 1 << 20;
    logon.allocator_handles = 1;
    logon.tpu_to_pack_capacity = 1024;
    logon.progress_tracker_capacity = 256;
    logon.pack_to_worker_capacity = 512;
    logon.worker_to_pack_capacity = 512;
    logon.pack_to_check_worker_capacity = 128;
    logon.check_worker_to_pack_capacity = 128;
    return logon;
}

static void test_agave_link() {
    // The logon is 1024 bytes: versions @0, ClientLogon @32, zeros after
    {
        FakeAgave agave{rejection("test"), {}};
        ClientLogon logon = test_logon();
        try {
            AgaveLink link{agave.server.path, logon};
        } catch (const std::runtime_error &) {
        }
        agave.finish();

        check(agave.logon.size() == 1024, "agave: logon is 1024 bytes");

        std::array<std::uint64_t, 4> versions{};
        if (agave.logon.size() == 1024) {
            std::memcpy(versions.data(), agave.logon.data(), 32);
        }
        // handshake 7, scheduler-bindings 6, shaq 3 << 16, rts-alloc 4 << 16
        check(versions == std::array<std::uint64_t, 4>{7, 6, 3 << 16, 4 << 16},
              "agave: versions match Agave 4.4.1");

        bool logon_ok =
            agave.logon.size() == 1024 &&
            std::memcmp(agave.logon.data() + 32, &logon, sizeof(logon)) == 0;
        check(logon_ok, "agave: ClientLogon sits at byte 32");

        bool rest_zero =
            agave.logon.size() == 1024 &&
            std::all_of(agave.logon.begin() + 32 + sizeof(ClientLogon),
                        agave.logon.end(),
                        [](std::byte b) { return b == std::byte{0}; });
        check(rest_zero, "agave: bytes after ClientLogon are zero");
    }

    // A rejection throws, with Agave's reason in the message
    {
        FakeAgave agave{rejection("Worker count; count=0"), {}};
        std::string what;
        try {
            AgaveLink link{agave.server.path, test_logon()};
        } catch (const std::runtime_error &e) {
            what = e.what();
        }
        check(what.find("Worker count; count=0") != std::string::npos,
              "agave: rejection throws with Agave's reason");
    }

    // Success with the wrong number of fds throws
    // (1 worker means 5 + 2 * 1 = 7 fds; send 3)
    {
        int pipe_fds[2];
        ::pipe(pipe_fds);
        int extra = ::dup(pipe_fds[0]);

        FakeAgave agave{bytes_of({0x01}), {pipe_fds[0], pipe_fds[1], extra}};
        bool threw{false};
        try {
            AgaveLink link{agave.server.path, test_logon()};
        } catch (const std::runtime_error &) {
            threw = true;
        }
        agave.finish();
        check(threw, "agave: wrong fd count throws");

        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::close(extra);
    }
}

static std::vector<std::byte> hex_bytes(std::string_view hex) {
    std::vector<std::byte> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::byte>(
            std::stoi(std::string{hex.substr(i, 2)}, nullptr, 16)));
    }
    return out;
}

static std::vector<std::byte> text_bytes(std::string_view text) {
    std::vector<std::byte> out;
    for (char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

static void test_base58() {
    // Standard Bitcoin base58 vectors
    check(base58_encode({}).empty(), "base58: empty input gives empty text");
    check(base58_encode(hex_bytes("61")) == "2g", "base58: 0x61 is 2g");
    check(base58_encode(hex_bytes("626262")) == "a3gV",
          "base58: 0x626262 is a3gV");
    check(base58_encode(text_bytes("Hello World!")) == "2NEpo7TZRRrLZSi2U",
          "base58: Hello World!");

    // Each leading zero byte becomes a '1'
    check(base58_encode(hex_bytes("00")) == "1", "base58: one zero byte is 1");
    check(base58_encode(hex_bytes("0000287fb4cd")) == "11233QC4",
          "base58: two leading zeros give two 1s");
    check(base58_encode(hex_bytes(
              "00eb15231dfceb60925886b67d065299925915aeb172c06647")) ==
              "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L",
          "base58: Bitcoin address");

    // Signature-sized inputs (64 bytes)
    check(base58_encode(std::vector<std::byte>(64)) == std::string(64, '1'),
          "base58: 64 zero bytes are 64 1s");
    check(base58_encode(std::vector<std::byte>(64, std::byte{0xff})) ==
              "67rpwLCuS5DGA8KGZXKsVQ7dnPb9goRLoKfgGbLfQg9WoLUgNY77E2jT11fem3"
              "coV9nAkguBACzrU1iyZM4B8roQ",
          "base58: 64 0xff bytes");
}

static bool producer_throws_on_join(FakeQueue &q) {
    try {
        SpscProducer<TestMsg> p{q.consumer_region()};
    } catch (const std::runtime_error &) {
        return true;
    }
    return false;
}

static void test_spsc_producer() {
    // Joining checks the header, same as the consumer
    {
        FakeQueue q{4};
        q.set_header(0, FakeQueue::version, q.mask);
        check(producer_throws_on_join(q), "spsc producer: wrong magic throws");
    }
    {
        FakeQueue q{4};
        q.set_header(FakeQueue::magic, 2 << 16, q.mask);
        check(producer_throws_on_join(q),
              "spsc producer: wrong version throws");
    }
    {
        FakeQueue q{4};
        q.set_header(FakeQueue::magic, FakeQueue::version, 7);
        check(producer_throws_on_join(q),
              "spsc producer: mask that doesn't fit the file throws");
    }

    // Capacity comes from the header
    {
        FakeQueue q{4};
        SpscProducer<TestMsg> p{q.consumer_region()};
        check(p.capacity() == 4, "spsc producer: capacity is 4");
    }

    // One write: lands in slot 0, write cursor moves to 1
    {
        FakeQueue q{4};
        SpscProducer<TestMsg> p{q.consumer_region()};
        check(p.try_write({1, 100}), "spsc producer: write to empty queue");
        TestMsg m = q.slot(0);
        check(m.id == 1 && m.value == 100,
              "spsc producer: message lands in slot 0");
        check(q.write().load() == 1, "spsc producer: write cursor moves to 1");
        check(q.read().load() == 0, "spsc producer: read cursor untouched");
    }

    // Full: 4 writes fit, the 5th is refused and changes nothing
    {
        FakeQueue q{4};
        SpscProducer<TestMsg> p{q.consumer_region()};
        bool all_fit = true;
        for (std::uint64_t i = 0; i < 4; ++i) {
            all_fit = all_fit && p.try_write({i, 0});
        }
        check(all_fit, "spsc producer: 4 writes fit in 4 slots");
        check(!p.try_write({99, 0}), "spsc producer: 5th write is refused");
        check(q.write().load() == 4 && q.slot(0).id == 0,
              "spsc producer: refused write changes nothing");

        // The consumer frees one slot: the next write wraps into slot 0
        q.pop();
        check(p.try_write({4, 0}), "spsc producer: write after a pop");
        check(q.slot(0).id == 4 && q.write().load() == 5,
              "spsc producer: wraps into the freed slot 0");
    }

    // Cursors never reset: position 6 lands in slot 2
    {
        FakeQueue q{4};
        q.skip_to(6);
        SpscProducer<TestMsg> p{q.consumer_region()};
        p.try_write({6, 0});
        check(q.slot(2).id == 6 && q.write().load() == 7,
              "spsc producer: position 6 lands in slot 2");
    }

    // Live consumer thread: 100,000 messages through 4 slots, none lost,
    // none out of order
    {
        constexpr std::uint64_t total = 100'000;
        FakeQueue q{4};
        SpscProducer<TestMsg> p{q.consumer_region()};

        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);

        std::uint64_t got{0};
        bool in_order{true};
        std::thread consumer{[&] {
            while (got < total && std::chrono::steady_clock::now() < deadline) {
                if (std::optional<TestMsg> m = q.pop()) {
                    in_order = in_order && m->id == got && m->value == got * 2;
                    ++got;
                }
            }
        }};

        for (std::uint64_t i = 0;
             i < total && std::chrono::steady_clock::now() < deadline;) {
            if (p.try_write({i, i * 2})) {
                ++i;
            }
        }
        consumer.join();
        check(got == total && in_order,
              "spsc producer: 100,000 messages to a live consumer, in order");
    }
}

static void test_rts_alloc() {
    // A file that isn't an rts-alloc region can't be joined
    {
        TestFile file{4096};
        int fd = file.open_fd();
        bool threw = false;
        try {
            Allocator allocator{fd};
        } catch (const std::runtime_error &) {
            threw = true;
        }
        check(threw, "rts-alloc: joining a non-allocator file throws");
        check(fd_is_open(fd), "rts-alloc: the fd stays with the caller");
        ::close(fd);
    }
}

int main() {
    test_stats();
    test_bench();
    test_socket();
    test_shm();
    test_spsc();
    test_spsc_producer();
    test_agave_link();
    test_base58();
    test_rts_alloc();

    std::println("\n{} failures.", failures);
    return failures == 0 ? 0 : 1;
}
