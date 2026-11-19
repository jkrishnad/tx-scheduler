#include "headers/agave_link.hpp"
#include "headers/bindings.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <print>
#include <string>
#include <thread>

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
                std::println("tx: offset:{}, length:{} flags:{}",
                             m->transaction.offset, m->transaction.length,
                             m->flags);
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
