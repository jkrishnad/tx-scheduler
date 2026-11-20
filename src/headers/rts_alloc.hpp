#pragma once
#include <cstdint>
#include <optional>

// C functions exported by rts-shim (Rust), around Agave's rts-alloc.
extern "C" {
struct RtsAllocator; // opaque: only the Rust side knows its layout
RtsAllocator *rts_join(int fd);
std::uint64_t rts_allocate(const RtsAllocator *handle, std::uint32_t size);
void rts_free(const RtsAllocator *handle, std::uint64_t offset);
void rts_clean_remote_frees(const RtsAllocator *handle);
void rts_close(RtsAllocator *handle);
}

// A handle on Agave's shared allocator. Allocations are offsets into the
// allocator region, the same offsets used in the scheduler messages.
class Allocator {
  public:
    // Joins the allocator in `fd`; the fd stays owned by the caller.
    // Throws std::runtime_error if it can't join.
    explicit Allocator(int fd);
    ~Allocator();

    Allocator(const Allocator &) = delete;
    Allocator &operator=(const Allocator &) = delete;
    Allocator(Allocator &&other) noexcept;

    // Offset of `size` fresh bytes, or nothing when the allocator is full.
    std::optional<std::uint64_t> allocate(std::uint32_t size);
    // Frees the block at `offset`, whichever process allocated it.
    void free(std::uint64_t offset);
    // Takes back blocks that Agave freed into this handle's slabs.
    void clean_remote_frees();

  private:
    RtsAllocator *handle_{nullptr};
};
