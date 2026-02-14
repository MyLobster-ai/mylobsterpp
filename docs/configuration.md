# Configuration

MyLobster++ is configured via a JSON file, environment variables, or a combination of both.

## Config File

Pass a config file via CLI flag or environment variable:

```bash
mylobsterpp -c config.json gateway
# or
OPENCLAW_CONFIG=config.json mylobsterpp gateway
```

### Full Example

```json
{
  "gateway": {
    "port": 18789,
    "bind": "loopback",
    "max_connections": 100,
    "auth": {
      "method": "token",
      "token": "${GATEWAY_TOKEN}"
    },
    "tools": {
      "allow": [],
      "deny": ["whatsapp_login"]
    }
  },
  "providers": [
    {
      "name": "anthropic",
      "api_key": "${ANTHROPIC_API_KEY}",
      "model": "claude-sonnet-4-20250514"
    },
    {
      "name": "ollama",
      "base_url": "http://127.0.0.1:11434",
      "model": "llama3.2"
    }
  ],
  "channels": [
    {
      "type": "telegram",
      "enabled": true,
      "history_limit": 50,
      "settings": {
        "bot_token": "${TELEGRAM_BOT_TOKEN}"
      }
    },
    {
      "type": "discord",
      "enabled": true,
      "settings": {
        "bot_token": "${DISCORD_BOT_TOKEN}",
        "presence_status": "online",
        "activity_name": "Helping users",
        "activity_type": 0,
        "auto_thread": true,
        "auto_thread_ttl_minutes": 10
      }
    }
  ],
  "memory": {
    "enabled": true,
    "store": "sqlite_vec",
    "similarity_threshold": 0.7
  },
  "browser": {
    "enabled": false,
    "pool_size": 2,
    "timeout_ms": 30000
  },
  "sessions": {
    "store": "sqlite",
    "ttl_seconds": 86400
  },
  "plugins": [],
  "cron": {
    "enabled": false
  },
  "routing": {
    "rules": [
      { "type": "scope", "scope": "guild", "id": "guild789" }
    ]
  },
  "log_level": "info"
}
```

## Environment Variable References

Config values can reference environment variables using `${VAR}` syntax:

```json
{
  "api_key": "${ANTHROPIC_API_KEY}"
}
```

### Resolution Rules

- `${VAR}` — Resolved to the value of environment variable `VAR` via `std::getenv()`
- If the variable is not set, the reference is preserved as-is (`${VAR}` stays in the config)
- Multiple references in one value are supported: `"${HOST}:${PORT}"`

### Escaping

Use `$${VAR}` to produce a literal `${VAR}` in the output:

```json
{
  "template": "$${NOT_RESOLVED}"
}
```

This resolves to the string `${NOT_RESOLVED}` without attempting env var lookup.

### Round-Trip Preservation

When writing config back to disk, unresolved `${VAR}` references are preserved. This means you can safely load, modify, and save a config file without losing env var references.

## Environment Variables

These environment variables are recognized without a config file:

| Variable | Config Path | Description |
|----------|-------------|-------------|
| `OPENCLAW_CONFIG` | — | Path to config file |
| `OPENCLAW_PORT` | `gateway.port` | Gateway listen port |
| `OPENCLAW_BIND` | `gateway.bind` | Bind mode: `loopback` or `all` |
| `OPENCLAW_LOG_LEVEL` | `log_level` | Log level |
| `ANTHROPIC_API_KEY` | `providers[0].api_key` | Anthropic provider key |
| `OPENAI_API_KEY` | `providers[].api_key` | OpenAI provider key |
| `HUGGINGFACE_API_KEY` | `providers[].api_key` | Hugging Face provider key |
| `OLLAMA_BASE_URL` | `providers[].base_url` | Ollama base URL |
| `SYNTHETIC_API_KEY` | `providers[].api_key` | Synthetic provider key |
| `GEMINI_API_KEY` | `providers[].api_key` | Gemini provider key |
| `TELEGRAM_BOT_TOKEN` | `channels[].settings.bot_token` | Telegram bot token |
| `DISCORD_BOT_TOKEN` | `channels[].settings.bot_token` | Discord bot token |

## History Limit

Per-channel message history compaction:

```json
{
  "channels": [{
    "type": "telegram",
    "history_limit": 50
  }]
}
```

When set, only the last N user/assistant message pairs (plus system messages) are sent to the LLM. This controls context window usage for channels with high message volume.

## Loading Priority

1. **Config file** — Base configuration
2. **Environment variables** — Override specific fields
3. **CLI flags** — Override port, bind, log level
4. **Defaults** — Fallback for anything unset

```cpp
#include "openclaw/core/config.hpp"

// Load from file
auto config = load_config("/path/to/config.json");

// Load from env only
auto config = load_config_from_env();

// Sensible defaults
auto config = default_config();

// Resolve env refs in a string
auto resolved = resolve_env_refs("${HOME}/data");
```
