#include "headers/rts_alloc.hpp"

#include <stdexcept>

namespace {
// rts_allocate's "no memory" value (u64::MAX on the Rust side).
constexpr std::uint64_t NO_MEMORY = UINT64_MAX;
} // namespace

Allocator::Allocator(int fd) : handle_(rts_join(fd)) {
    if (handle_ == nullptr) {
        throw std::runtime_error("rts-alloc: can't join the allocator");
    }
}

Allocator::~Allocator() { rts_close(handle_); }

Allocator::Allocator(Allocator &&other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

std::optional<std::uint64_t> Allocator::allocate(std::uint32_t size) {
    std::uint64_t offset = rts_allocate(handle_, size);
    if (offset == NO_MEMORY) {
        return std::nullopt;
    }
    return offset;
}

void Allocator::free(std::uint64_t offset) { rts_free(handle_, offset); }

void Allocator::clean_remote_frees() { rts_clean_remote_frees(handle_); }
