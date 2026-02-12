#include "openclaw/infra/ports.hpp"
#include "openclaw/core/logger.hpp"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openclaw::infra {

#ifdef _WIN32
namespace {
struct WinsockInit {
    WinsockInit() {
        WSADATA wsa;
        ::WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockInit() { ::WSACleanup(); }
};
static WinsockInit winsock_init_;
} // namespace
#endif

auto is_port_available(uint16_t port) -> bool {
#ifdef _WIN32
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create socket: error {}", ::WSAGetLastError());
        return false;
    }

    // Allow immediate reuse
    const char opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool available = (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr),
                             sizeof(addr)) == 0);

    ::closesocket(sock);
    return available;
#else
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("Failed to create socket: {}", std::strerror(errno));
        return false;
    }

    // Allow immediate reuse
    int opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool available = (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr),
                             sizeof(addr)) == 0);

    ::close(sock);
    return available;
#endif
}

auto find_free_port(uint16_t start_port, uint16_t max_attempts)
    -> std::optional<uint16_t> {
    for (uint16_t i = 0; i < max_attempts; ++i) {
        auto candidate = static_cast<uint16_t>(start_port + i);
        // Guard against overflow past valid port range
        if (candidate < start_port) {
            break;
        }
        if (is_port_available(candidate)) {
            LOG_DEBUG("Found free port: {}", candidate);
            return candidate;
        }
    }
    LOG_WARN("No free port found in range [{}, {})",
             start_port, start_port + max_attempts);
    return std::nullopt;
}

} // namespace openclaw::infra
