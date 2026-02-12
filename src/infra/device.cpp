#include "openclaw/infra/device.hpp"
#include "openclaw/core/logger.hpp"
#include "openclaw/core/utils.hpp"

#include <string>

#include <sys/utsname.h>
#include <unistd.h>

namespace openclaw::infra {

auto get_device_identity() -> openclaw::DeviceIdentity {
    DeviceIdentity identity;

    // Get hostname
    char hostname_buf[256] = {};
    if (::gethostname(hostname_buf, sizeof(hostname_buf)) == 0) {
        identity.hostname = hostname_buf;
    } else {
        identity.hostname = "unknown";
        LOG_WARN("Failed to get hostname, using 'unknown'");
    }

    // Get OS and architecture via uname
    struct utsname uname_info {};
    if (::uname(&uname_info) == 0) {
        identity.os = uname_info.sysname;   // e.g. "Linux", "Darwin"
        identity.arch = uname_info.machine;  // e.g. "x86_64", "aarch64", "arm64"
    } else {
        identity.os = "unknown";
        identity.arch = "unknown";
        LOG_WARN("Failed to get uname info");
    }

    // Generate a stable device ID by hashing hostname + os + arch
    auto raw = identity.hostname + ":" + identity.os + ":" + identity.arch;
    identity.device_id = openclaw::utils::sha256(raw).substr(0, 16);

    LOG_DEBUG("Device identity: id={}, host={}, os={}, arch={}",
              identity.device_id, identity.hostname,
              identity.os, identity.arch);

    return identity;
}

} // namespace openclaw::infra
