#include "headers/agave_link.hpp"
#include "headers/base58.hpp"
#include "headers/bindings.hpp"
#include "headers/shm.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <thread>

static std::optional<std::string>
tx_signature(const SharedRegion &alloc,
             const SharableTransactionRegion &region) {

    std::size_t offset = region.offset;
    std::size_t length = region.length;
    if (offset > alloc.size() || length > alloc.size() - offset) {
        return std::nullopt;
    }

    std::span<const std::byte> tx{alloc.data() + offset, length};
    if (tx.size() < 1 + 64 || tx[0] == std::byte{0} ||
        (tx[0] & std::byte{0x80}) != std::byte{0}) {
        return std::nullopt;
    }

    return base58_encode(tx.subspan(1, 64));
}
int main(int argc, char **argv) {
    if (argc != 2) {
        std::println(stderr,
                     "usage: scheduler <ledger>/scheduler_bindings.ipc");
        return 1;
    }
    std::string path = argv[1];
    std::println("scheduler started");

    ClientLogon logon{};

    // 1 worker and 1 check worker. The allocator gets 64 MiB because
    // Agave splits it into 2 MiB slabs shared by 4 handles.
    logon.worker_count = 1;
    logon.check_worker_count = 1;
    logon.allocator_size = 64 << 20;
    logon.allocator_handles = 1;
    logon.tpu_to_pack_capacity = 1024;
    logon.progress_tracker_capacity = 256;
    logon.pack_to_worker_capacity = 512;
    logon.worker_to_pack_capacity = 512;
    logon.pack_to_check_worker_capacity = 128;
    logon.check_worker_to_pack_capacity = 128;

    try {
        AgaveLink link{path, logon};
        std::println("connected: tpu_to_pack= {} slots, progress= {} slots",
                     link.tpu_to_pack().capacity(), link.progress().capacity());

        std::uint64_t last_slot = 0;
        while (true) {
            bool idle = true;

            if (auto p = link.progress().try_read()) {
                idle = false;
                if (p->current_slot > last_slot) {
                    last_slot = p->current_slot;
                    std::println("slot {}, leader_state {}", p->current_slot,
                                 p->leader_state);
                }
            }

            if (auto m = link.tpu_to_pack().try_read()) {
                idle = false;
                if (auto sig = tx_signature(link.allocator(), m->transaction)) {
                    std::println("tx {} len={} flags={}", *sig,
                                 m->transaction.length, m->flags);
                } else {
                    std::println(stderr, "tx: bad region offset={} len={}",
                                 m->transaction.offset, m->transaction.length);
                }
            }

            if (idle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } catch (const std::exception &e) {
        std::println(stderr, "scheduler failed: {}", e.what());
        return 1;
    }
}
