#pragma once

#include "headers/shm.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Layout of a shaq 4.4.0 SPSC queue in shared memory.
namespace shaq {
inline constexpr std::uint64_t MAGIC = 0x7368617173707363; // "shaqspsc"
inline constexpr std::uint32_t VERSION = 3 << 16;

inline constexpr std::size_t MAGIC_OFFSET = 0;
inline constexpr std::size_t VERSION_OFFSET = 8;
inline constexpr std::size_t MASK_OFFSET = 12;
inline constexpr std::size_t WRITE_OFFSET = 64;
inline constexpr std::size_t READ_OFFSET = 128;
inline constexpr std::size_t SLOTS_OFFSET = 256;
} // namespace shaq

template <typename T> std::size_t shaq_join(const SharedRegion &region) {
    if (region.size() < shaq::SLOTS_OFFSET) {
        throw std::runtime_error("spsc: region smaller than the header");
    }

    std::uint64_t magic{};
    std::uint32_t version{};
    std::uint32_t mask{};
    std::memcpy(&magic, region.data() + shaq::MAGIC_OFFSET, sizeof(magic));
    std::memcpy(&version, region.data() + shaq::VERSION_OFFSET,
                sizeof(version));
    std::memcpy(&mask, region.data() + shaq::MASK_OFFSET, sizeof(mask));

    if (magic != shaq::MAGIC) {
        throw std::runtime_error("spsc: wrong magic");
    }
    if (version != shaq::VERSION) {
        throw std::runtime_error("spsc: wrong version");
    }

    // shaq rounds the slot count down to a power of two.
    std::size_t items = (region.size() - shaq::SLOTS_OFFSET) / sizeof(T);
    std::size_t cap{1};
    while (cap * 2 <= items) {
        cap *= 2;
    }
    if (items == 0 || std::size_t{mask} + 1 != cap) {
        throw std::runtime_error("spsc: mask doesn't fit the region");
    }

    return mask;
}

template <typename T> class SpscConsumer {
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    explicit SpscConsumer(SharedRegion region)
        : region_(std::move(region)), mask_(shaq_join<T>(region_)) {}

    std::optional<T> try_read() {
        std::atomic_ref<uint64_t> write = cursor(shaq::WRITE_OFFSET);
        std::atomic_ref<uint64_t> read = cursor(shaq::READ_OFFSET);

        uint64_t w = write.load(std::memory_order_acquire);
        uint64_t r = read.load(std::memory_order_relaxed);

        if (w == r) {
            return std::nullopt;
        }

        size_t slot_ind = r & mask_;
        std::byte *byte_address =
            region_.data() + shaq::SLOTS_OFFSET + slot_ind * sizeof(T);
        T value;
        std::memcpy(&value, byte_address, sizeof(T));

        read.store(r + 1, std::memory_order_release);
        return value;
    }

    std::size_t capacity() const { return mask_ + 1; };

  private:
    SharedRegion region_;
    std::size_t mask_{0};

    std::atomic_ref<uint64_t> cursor(size_t offset) {
        return std::atomic_ref<std::uint64_t>{
            *reinterpret_cast<std::uint64_t *>(region_.data() + offset)};
    };
};

template <typename T> class SpscProducer {
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    explicit SpscProducer(SharedRegion region)
        : region_(std::move(region)), mask_(shaq_join<T>(region_)) {}

    bool try_write(const T &value) {
        std::atomic_ref<uint64_t> write = cursor(shaq::WRITE_OFFSET);
        std::atomic_ref<uint64_t> read = cursor(shaq::READ_OFFSET);

        uint64_t w = write.load(std::memory_order_relaxed);
        uint64_t r = read.load(std::memory_order_acquire);

        if (w - r >= capacity()) {
            return false;
        }

        size_t slot_ind = w & mask_;
        std::memcpy(region_.data() + shaq::SLOTS_OFFSET + slot_ind * sizeof(T),
                    &value, sizeof(T));
        write.store(w + 1, std::memory_order_release);
        return true;
    };
    std::size_t capacity() const { return mask_ + 1; };

  private:
    SharedRegion region_;
    std::size_t mask_{0};

    std::atomic_ref<uint64_t> cursor(size_t offset) {
        return std::atomic_ref<std::uint64_t>{
            *reinterpret_cast<std::uint64_t *>(region_.data() + offset)};
    };
};
