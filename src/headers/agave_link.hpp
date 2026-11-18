#pragma once
#include "headers/bindings.hpp"
#include "headers/shm.hpp"
#include "headers/spsc.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agave_versions {
inline constexpr std::uint64_t HANDSHAKE = 7;
inline constexpr std::uint64_t SCHEDULER_BINDINGS = 6;
inline constexpr std::uint64_t SHAQ = 3 << 16;
inline constexpr std::uint64_t RTS_ALLOC = 4 << 16;
} // namespace agave_versions

class AgaveLink {
  public:
    AgaveLink(const std::string &path, const ClientLogon &logon);

    SpscConsumer<TpuToPackMessage> &tpu_to_pack() { return *tpu_to_pack_; }
    SpscConsumer<ProgressMessage> &progress() { return *progress_; }

  private:
    std::vector<SharedRegion> regions_;
    std::optional<SpscConsumer<TpuToPackMessage>> tpu_to_pack_;
    std::optional<SpscConsumer<ProgressMessage>> progress_;
};
