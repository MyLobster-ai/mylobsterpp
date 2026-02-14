# MyLobster++ Documentation

Architecture and feature documentation for the MyLobster++ C++23 AI assistant platform.

## Guides

| Document | Description |
|----------|-------------|
| [Providers](providers.md) | All 7 LLM providers: Anthropic, OpenAI, Gemini, Bedrock, Hugging Face, Ollama, Synthetic |
| [Channels](channels.md) | Discord voice/presence/autothread, Telegram voice/menu, Slack gating, WhatsApp, Signal, LINE |
| [Delivery Queue](delivery-queue.md) | Write-ahead queue: persistence, retry, crash recovery, message hooks |
| [Security](security.md) | Tool policy, SSRF protection, header sanitization, SIGUSR1 cleanup |
| [Routing](routing.md) | Binding scopes (Peer/Guild/Team/Global), scope rules, session keys |
| [Configuration](configuration.md) | Config file format, `${VAR}` env refs, `$${VAR}` escaping, history limit |
| [Cron](cron.md) | Cron expressions, scheduler, one-shot tasks with deleteAfterRun |
| [Sessions](sessions.md) | Session management, agentId, SQLite store, transcript archival |

## Quick Start

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run gateway
./build/mylobsterpp gateway

# Run tests
cd build && ctest --output-on-failure
```

See the main [README](../README.md) for full build instructions and CLI usage.
