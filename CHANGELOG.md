# Changelog

All notable changes to MyLobster++ are documented in this file.

## v2026.2.17

Port of [OpenClaw v2026.2.17](https://github.com/openclaw/openclaw) changes to C++23.

### Model Updates

- **Claude Sonnet 4.6** — Default model updated to `claude-sonnet-4-6-20250514`. Added `claude-opus-4-6-20250514` and `claude-sonnet-4-6-20250514` to Anthropic model catalog.
- **1M Context Beta** — `anthropic-beta: context-1m-2025-08-07` header sent for eligible Claude 4+ models. Enables 1M token context window.
- **HuggingFace catalog reorder** — Llama 4 variants moved to top of static catalog.

### Configuration

- **Subagent limits** — New `SubagentConfig` with `max_spawn_depth` (1-5) and `max_children_per_agent` (1-20) fields.
- **Image config** — New `ImageConfig` with `max_dimension_px` (default 1200) and `max_bytes` (default 5MB) fields.
- **Cron stagger** — `CronConfig::default_stagger_ms` for jitter on cron job execution.

### Cron

- **Stagger delay** — `ScheduledTask::stagger_ms` field. Applies a `steady_timer` delay before task execution to reduce thundering herd on shared-minute schedules.

## v2026.2.13

Port of [OpenClaw v2026.2.13](https://github.com/openclaw/openclaw) changes to C++23.

### New Providers

- **Hugging Face Inference** — OpenAI-compatible API at `https://router.huggingface.co/v1`. Static catalog of ~20 models with 131072 context / 8192 max tokens. Route policy suffixes (`:cheapest`, `:fastest`) stripped from model name and applied as header hints. Dynamic model discovery via `GET /v1/models` with fallback to static catalog. Reasoning detection via regex matching on model ID.
- **Ollama** — Native NDJSON streaming provider for local LLMs at `http://127.0.0.1:11434`. Uses `/api/chat` endpoint with line-by-line JSON parsing (not SSE). Tool call accumulation across chunks, finalized on `"done": true`. Image support via base64 `"images"` array. `toolResult` role mapped to Ollama `tool` role.
- **Synthetic Catalog** — Anthropic-compatible API at `https://api.synthetic.new/anthropic`. 22 models including MiniMax M2.1, DeepSeek R1/V3, Qwen3, GLM-4.5/4.6/5, Llama 3.3/4, and Kimi K2. Model IDs use `hf:org/model-name` format with `resolve_hf_model()` mapping.
- **OpenAI Codex Spark** — Added `gpt-5.3-codex-spark` to the OpenAI provider model catalog.

### Write-Ahead Delivery Queue

- **File-based persistence** at `~/.openclaw/delivery-queue/`. Atomic writes via `.tmp` + `std::filesystem::rename()`. Failed entries moved to `failed/` subdirectory after max retries.
- **Exponential backoff** retry schedule: 5s, 25s, 2m, 10m for retries 1-4, max 5 attempts.
- **Crash recovery** — On gateway start, loads pending queue entries sorted oldest-first with 60s wall-clock recovery deadline.
- **DeliveredChannel wrapper** — Decorator pattern wrapping any `Channel` implementation. Integrates `message_sending` (pre-send cancel/modify) and `message_sent` (post-send tracking) hooks. Minimizes changes to existing channel implementations.

### Channel Improvements

- **Discord voice messages** — 3-step CDN upload (request URL, PUT file, POST message with `kVoiceMessageFlag | kSuppressNotificationsFlag`). PCM waveform generation: 16-bit LE samples -> 256 amplitude values (0-255) -> base64.
- **Discord configurable presence** — `presence_status`, `activity_name`, `activity_type`, `activity_url` in config. Sent in IDENTIFY gateway payload.
- **Discord AutoThread** — `create_thread()` via POST to `/channels/{id}/messages/{msg}/threads`, name truncated to 100 chars. Configurable via `auto_thread` and `auto_thread_ttl_minutes`.
- **Slack thread-ownership gating** — `@<BOT_USER_ID>` mention bypass before outbound gating check.
- **Telegram voice routing** — `is_voice_compatible()` checks extensions (.mp3, .m4a, .ogg, .oga, .opus). Compatible audio sent via `sendVoice` instead of `sendDocument`.
- **Telegram 100-command bot menu** — `build_capped_menu_commands()` validates regex `[a-z0-9_]{1,32}`, deduplicates, caps at 100 with overflow warning.
- **WhatsApp filename preservation** — Document attachments now include `filename` in the media object.
- **Signal arm64 detection** — Compile-time warning on arm64 Linux with Homebrew/GitHub install instructions for `signal-cli`.

### Gateway & Security

- **Tool invocation gating** — `ToolPolicy` with owner-only tools (`whatsapp_login`), tool groups (`group:sessions`, `group:automation`), profiles (Minimal, Coding, Messaging, Full), and `gateway.tools.{allow,deny}` config overrides.
- **SSRF blocking** — `FetchGuard` validates URLs against private IP ranges: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.0/8, 169.254.0.0/16, 100.64.0.0/10, fc00::/7, ::1. DNS resolution before fetch, max 3 redirects with loop detection.
- **SIGUSR1 restart cleanup** — Registered via `boost::asio::signal_set`. Clears connections, resets running flag, drains coroutines.
- **WebSocket header sanitization** — Truncates header values to 200 chars, strips control characters before logging.

### Sessions, Config, Routing

- **Session agentId** — `std::optional<std::string> agent_id` added to Session. Session key format: `"{agent_id}:{channel}:{peer_id}"`. SQLite schema migrated with `agent_id TEXT` column and index.
- **Config `${VAR}` preservation** — `resolve_env_refs()` resolves `${VAR}` via `std::getenv()`, preserves unresolved refs. `$${VAR}` escapes to literal `${VAR}`.
- **Binding-scope routing** — `BindingScope` enum (Peer, Guild, Team, Global) with `BindingContext` in `IncomingMessage`. `ScopeRule` matches peer_id, guild_id, or team_id based on scope.
- **History limit** — `std::optional<int> history_limit` added to `ChannelConfig` for per-channel history compaction.

### Agent, Cron, Memory

- **Pre-prompt diagnostics** — Logs message count, system prompt chars, prompt chars, provider name, and model before each LLM call.
- **Cron deleteAfterRun** — `bool delete_after_run = false` on `ScheduledTask`. One-shot tasks auto-removed after successful execution.
- **Embedding provider chain** — `EmbeddingProviderChain` tries providers in order (local -> cloud). Falls back gracefully on provider failure.

### Tests

Added 11 Catch2 test files covering all new features:

| File | Coverage |
|------|----------|
| `tests/providers/test_huggingface.cpp` | Catalog, route policy suffixes, reasoning detection |
| `tests/providers/test_ollama.cpp` | NDJSON parsing, tool call accumulation, message conversion |
| `tests/providers/test_synthetic.cpp` | Catalog lookup, `hf:` prefix, reasoning flags |
| `tests/infra/test_delivery_queue.cpp` | Enqueue/ack/fail lifecycle, backoff, file persistence |
| `tests/infra/test_fetch_guard.cpp` | Private IP detection for all RFC ranges |
| `tests/gateway/test_tool_policy.cpp` | Owner-only, group expansion, profiles, allow/deny |
| `tests/channels/test_discord_voice.cpp` | Voice flags, waveform gen, thread name, presence config |
| `tests/channels/test_telegram_voice.cpp` | MIME detection, command menu cap at 100 |
| `tests/core/test_config_env_refs.cpp` | `${VAR}` resolution, `$${VAR}` escaping |
| `tests/routing/test_scope_rules.cpp` | Peer, guild, team, global scope matching |
| `tests/cron/test_delete_after_run.cpp` | One-shot auto-deletion |
