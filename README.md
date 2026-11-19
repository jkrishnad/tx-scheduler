# tx-scheduler

An external C++ transaction scheduler for Agave validators. It uses Agave's scheduler bindings, enabled with `--enable-scheduler-bindings`, which let a separate process take over the "pack" stage: deciding which transactions go into a block.

Current state: it connects to an Agave validator and reads all incoming transactions. It can't send them back to the validator for execution yet.

## How it works

- **Connect:** open the Unix socket Agave creates at `<ledger>/scheduler_bindings.ipc`.
- **Logon:** send 1024 bytes: the protocol versions (handshake 7, bindings 6, shaq, rts-alloc) plus a `ClientLogon`, the session request (worker counts, queue sizes, allocator size, etc.).
- **Reply:** Agave answers with one of two replies:
  - accepted (`0x01`) + file descriptors passed over the socket
  - rejected (`0x02`) + the reason for the rejection
- **Map:** each fd is a shared-memory region, mapped into the scheduler's memory with `mmap`.
  - fd 0: the allocator, where the transaction bytes live
  - fd 1: `tpu_to_pack`, new transactions to be packed
  - fd 2: `progress_tracker`, slot and leader state
  - fd 3, 4: check worker queues
  - fd 5+: a pair of queues for each execution worker
- **Read:** poll the queues. Each transaction message holds an offset into the allocator; read the bytes there, take the first signature, and base58-encode it.
- **Reconnect:** if no progress message arrives for 2 seconds, the session is treated as lost and the scheduler logs on again.

## Running locally

Needs CMake 3.28+, a C++23 compiler, and Agave / Solana CLI 4.4.1.

Build and run the tests:

```bash
cmake -B build && cmake --build build
./build/tests
```

Start a local validator with the bindings enabled (keep the ledger path short: Unix socket paths are limited to about 104 bytes on macOS):

```bash
solana-test-validator --ledger ~/.tvl --reset --enable-scheduler-bindings --alpenglow \
  --mint "$(solana address -k ~/.config/solana/id.json)"
```

In a second terminal, start the scheduler:

```bash
./build/scheduler ~/.tvl/scheduler_bindings.ipc
```

In a third terminal, send some test transfers (100 transfers, 10 per second):

```bash
scripts/traffic.sh 100 10
```

Without `--alpenglow`, the test validator runs Tower consensus, and its vote transactions show up in the scheduler too.
