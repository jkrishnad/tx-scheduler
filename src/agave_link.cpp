#include "headers/agave_link.hpp"
#include "headers/bindings.hpp"
#include "headers/socket.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <utility>

AgaveLink::AgaveLink(const std::string &path, const ClientLogon &logon) {
    UnixSocket socket{path};

    std::array<std::byte, 1024> buf{};

    ProtocolVersions versions{
        agave_versions::HANDSHAKE,
        agave_versions::SCHEDULER_BINDINGS,
        agave_versions::SHAQ,
        agave_versions::RTS_ALLOC,
    };
    std::memcpy(buf.data(), &versions, sizeof(versions));
    std::memcpy(buf.data() + sizeof(versions), &logon, sizeof(logon));

    socket.send_all(buf);
    Reply r = socket.recv_with_fds();

    if (r.bytes.empty()) {
        throw std::runtime_error("Agave failed during handshake");
    }

    if (r.bytes.size() >= 2 && r.bytes[0] == std::byte{0X02}) {
        std::size_t len = static_cast<std::size_t>(r.bytes[1]);
        std::string reason(reinterpret_cast<const char *>(r.bytes.data() + 2),
                           std::min<std::size_t>(len, r.bytes.size() - 2));
        throw std::runtime_error("Agave rejected logon: " + reason);
    }

    if (r.fds.size() != 5 + 2 * logon.worker_count) {
        for (int fd : r.fds) {
            close(fd);
        }
        throw std::runtime_error("Agave returned incorrect number of fds");
    }

    std::vector<SharedRegion> regions;
    regions.reserve(r.fds.size());

    for (int fd : r.fds) {
        regions.emplace_back(fd);
    }

    tpu_to_pack_.emplace(std::move(regions[1]));
    progress_.emplace(std::move(regions[2]));

    regions_ = std::move(regions);
}
