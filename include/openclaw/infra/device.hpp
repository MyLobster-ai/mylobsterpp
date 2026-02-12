#pragma once

#include "openclaw/core/types.hpp"

namespace openclaw::infra {

/// Detects and returns the current device identity.
/// Populates hostname, OS name, architecture, and a stable device ID
/// derived from hashing the hostname + OS + arch combination.
auto get_device_identity() -> openclaw::DeviceIdentity;

} // namespace openclaw::infra
