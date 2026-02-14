# Security

MyLobster++ includes several security hardening features to protect against common attack vectors.

## Tool Invocation Gating

The `ToolPolicy` system controls which tools can be invoked and by whom.

### Owner-Only Tools

Certain tools require owner-level authorization:

- `whatsapp_login` — WhatsApp authentication flow

### Tool Groups

Groups provide shorthand for sets of related tools:

| Group | Tools |
|-------|-------|
| `group:sessions` | `spawn`, `send`, `list` |
| `group:automation` | `gateway`, `cron` |

### Profiles

Predefined tool profiles control the default tool set:

| Profile | Description |
|---------|-------------|
| Minimal | Core tools only |
| Coding | Minimal + development tools |
| Messaging | Minimal + channel/messaging tools |
| Full | All tools enabled |

### Allow/Deny Overrides

Configuration-level overrides take highest priority:

```json
{
  "gateway": {
    "tools": {
      "allow": ["custom_tool"],
      "deny": ["dangerous_tool"]
    }
  }
}
```

Priority order: **deny > owner-only > allow > profile defaults**.

## SSRF Protection

The `FetchGuard` validates all outbound HTTP requests to prevent Server-Side Request Forgery.

### Blocked IP Ranges

| Range | Description |
|-------|-------------|
| `10.0.0.0/8` | RFC 1918 private |
| `172.16.0.0/12` | RFC 1918 private |
| `192.168.0.0/16` | RFC 1918 private |
| `127.0.0.0/8` | Loopback |
| `169.254.0.0/16` | Link-local |
| `100.64.0.0/10` | CGNAT (RFC 6598) |
| `fc00::/7` | IPv6 ULA |
| `::1` | IPv6 loopback |
| `fe80::/10` | IPv6 link-local |

### URL Validation Flow

1. Parse URL and extract hostname
2. DNS resolve hostname to IP addresses
3. Check all resolved IPs against blocked ranges
4. Reject if any IP is private

### Redirect Following

`safe_fetch()` follows up to 3 HTTP redirects with:

- Loop detection (visited URL set)
- Each redirect target re-validated against blocked IP ranges
- Prevents SSRF via DNS rebinding on redirects

### Usage

```cpp
#include "openclaw/infra/fetch_guard.hpp"

using namespace openclaw::infra;

FetchGuard guard;

// Check a single IP
bool private_ip = guard.is_private_ip("10.0.0.1");     // true
bool public_ip = guard.is_private_ip("8.8.8.8");       // false

// Validate a URL (DNS resolves and checks)
auto result = guard.validate_url("https://example.com"); // Ok
auto blocked = guard.validate_url("http://192.168.1.1"); // Error

// Safe fetch with redirect validation
auto response = co_await guard.safe_fetch(ioc, "https://example.com");
```

## WebSocket Header Sanitization

Before logging WebSocket handshake headers:

- Header values truncated to 200 characters
- Control characters (0x00-0x1F, 0x7F) stripped
- Prevents log injection and log overflow attacks

## SIGUSR1 Restart Cleanup

Sending `SIGUSR1` to the gateway process triggers a clean restart:

1. Clear active connections map
2. Reset running flag
3. Drain pending coroutines
4. Log cleanup summary

This prevents zombie state accumulation after hot restarts.
