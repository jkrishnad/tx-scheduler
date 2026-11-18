#pragma once
#include "headers/bindings.hpp"
#include <cstdint>
#include <string>

namespace agave_versions {
inline constexpr std::uint64_t HANDSHAKE = 7;
inline constexpr std::uint64_t SCHEDULER_BINDINGS = 6;
inline constexpr std::uint64_t SHAQ = 3 << 16;
inline constexpr std::uint64_t RTS_ALLOC = 4 << 16;
} // namespace agave_versions

class AgaveLink {
  public:
    AgaveLink(const std::string &path, const ClientLogon &logon);
};
