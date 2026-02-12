#pragma once

#include <cstdint>
#include <optional>

namespace openclaw::infra {

/// Checks whether the given TCP port is available for binding on localhost.
auto is_port_available(uint16_t port) -> bool;

/// Finds the first available TCP port starting from `start_port`.
/// Scans up to `max_attempts` ports sequentially.
/// Returns std::nullopt if no free port is found within the range.
auto find_free_port(uint16_t start_port = 18789,
                    uint16_t max_attempts = 100) -> std::optional<uint16_t>;

} // namespace openclaw::infra
