# Changelog

All notable changes to MyLobster++ are documented in this file.

## v2026.2.24

Port of [OpenClaw v2026.2.23](https://github.com/openclaw/openclaw) and [v2026.2.24](https://github.com/openclaw/openclaw) changes to C++23.

### Breaking Changes

- **Browser SSRF policy default** — Default SSRF policy flipped to trusted-network mode (`dangerously_allow_private_network=true`). New `SsrfPolicyConfig` struct with dual-key resolution (`allow_private_network` legacy, `dangerously_allow_private_network` canonical). Explicit `false` re-enables private IP blocking.
- **Heartbeat DM delivery blocking** — `HeartbeatConfig::target` default changed from `"last"` to `"none"`. `ChatType` enum and per-channel DM inference functions (`infer_telegram_target_chat_type()`, `infer_discord_target_chat_type()`, etc.) block heartbeats to DM channels unless explicitly allowed.
- **Docker sandbox namespace-join blocking** — `is_dangerous_network_mode()` blocks `host` and `container:*` network modes by default. Break-glass override via `SandboxDockerSettings::dangerously_allow_container_namespace_join`.

### Security Fixes

- **Cross-channel reply routing hardening** — `TurnSourceMetadata` struct with channel/to/account_id/thread_id fields pins reply routing to turn-scoped metadata, preventing cross-channel reply injection.
- **Unauthorized WebSocket flood guard** — `UnauthorizedFloodGuard` tracks per-connection rejection count, closes connection after threshold, and samples duplicate rejection logs.
- **Sandbox hardlink/symlink escape prevention** — `assert_no_hardlinked_final_path()` checks `stat.nlink > 1` to detect hardlink-based sandbox escapes. `normalize_at_prefix()` strips leading `@` from paths.
- **Exec approval hardening** — `unwrap_shell_wrapper_argv()` with depth cap, `resolve_inline_command_token_index()`, `has_trailing_positional_argv()` for shell wrapper detection. Fail-closed on depth cap exceeded.
- **Safe-bin trusted directory hardening** — `classify_risky_safe_bin_dir()` flags temporary, package-manager, and home-scoped paths. Default trusted dirs restricted to `{"/bin", "/usr/bin"}`.
- **Telegram DM authorization** — `is_dm_authorized()` enforces DM policy (`open`/`allowlist`) before processing media downloads. Inbound activity tracking deferred until after authorization.
- **Workspace @-prefix path normalization** — `normalize_at_prefix()` strips leading `@` before workspace boundary checks to prevent path traversal.
- **WhatsApp reasoning payload suppression** — `suppress_reasoning_payload()` hard-drops payloads marked as reasoning and text starting with `Reasoning:`.
- **Trusted proxy auth for Control UI** — `AuthInfo::trusted_proxy_auth_ok` allows Control UI connections behind trusted reverse proxies without device pairing.
- **Sandbox bind-mount source path canonicalization** — `canonicalize_bind_mount_source()` resolves via existing-ancestor realpath to prevent symlink-parent bypass.
- **Multi-user heuristic security flag** — `collect_multi_user_findings()` inspects config for open group/DM policies, wildcard allowlists, and unsandboxed tool exposure.

### New Providers

- **Kilo Gateway** — `KilocodeProvider` with Anthropic-compatible API. Default model `kilocode/anthropic/claude-opus-4.6`. Implicit provider detection via `is_kilocode_model()`.
- **Vercel AI Gateway normalization** — `normalize_vercel_model_ref()` maps `vercel-ai-gateway/claude-*` refs to canonical Anthropic model IDs.

### Protocol & Gateway

- **tools.catalog RPC** — New `tools.catalog` method returns tool registry grouped by subsystem with plugin provenance labeling. `ToolCatalogEntry`, `ToolCatalogGroup`, `ToolCatalogProfile`, `ToolsCatalogResult` structs.
- **Cron list/runs paging & sorting** — `CronListParams` and `CronRunsParams` extended with `limit`, `offset`, `query`, `sort_by`, `sort_dir`, status/delivery filters, and `scope` field.
- **Talk provider-agnostic config** — `TalkProviderConfig` and `TalkConfig` structs with `normalize_talk_config()` for legacy flat-field migration, `resolve_active_talk_provider()`, and `build_talk_config_response()`.
- **HTTP HSTS headers** — `GatewayConfig::http_security_hsts` emits `Strict-Transport-Security` header on WebSocket connections when configured.

### Channel & Provider Fixes

- **Telegram empty markdown retry** — On 400 status with `parse_mode` set, retries send without parse_mode.
- **Discord DAVE voice config** — `dave_encryption` and `decryption_failure_tolerance` config fields for DAVE protocol support.
- **Slack DM routing fix** — `D*` channel IDs treated as DMs regardless of `channel_type` field.
- **WhatsApp close 440 non-retryable** — Status 440 treated as non-retryable session error, halts reconnection.
- **Typing keepalive** — `Channel::refresh_typing()` and `clear_typing_keepalive()` virtual methods for long-reply typing indicators.
- **Gemini reasoning budget sanitization** — Negative `thinkingBudget` dropped, `ThinkingMode` mapped to `thinkingLevel` (`HIGH`/`MEDIUM`).
- **SiliconFlow thinking normalization** — `thinking="off"` normalized to null for `Pro/*` models.
- **Bedrock alias normalization** — `normalize_provider_alias()` maps `bedrock`, `aws-bedrock`, `aws_bedrock`, `amazon bedrock` to `amazon-bedrock`.
- **Agent model fallback chain** — `set_fallback_providers()` enables fallback chain traversal during cooldown instead of collapsing to primary.

### Config & Runtime

- **Config meta timestamp coercion** — Numeric `meta.lastTouchedAt` coerced to ISO 8601 string during config load.
- **Google antigravity removal compat** — `google-antigravity-auth` plugin references logged as warnings and stripped during config load.
- **bestEffortDeliver agent param** — `AgentRuntime::best_effort_deliver` optional bool for delivery-failure tolerance.
- **Bootstrap file caching** — `SessionManager::cache_bootstrap()`, `get_cached_bootstrap()`, `invalidate_bootstrap_cache()` for session-key-scoped bootstrap snapshots.
- **Auto-reply multilingual stop phrases** — `AgentRuntime::is_stop_phrase()` matches `stop openclaw`, `please stop`, trailing punctuation tolerance, and keywords in ES/FR/ZH/JP/HI/AR/RU/DE.

### Tests

Added 12 Catch2 test files covering new features:

| File | Coverage |
|------|----------|
| `tests/providers/test_kilocode.cpp` | Provider constants, model detection |
| `tests/providers/test_openai.cpp` | Vercel model normalization |
| `tests/gateway/test_tools_catalog.cpp` | Catalog entry/group/profile JSON, build function |
| `tests/gateway/test_talk_config.cpp` | Talk config normalization, legacy compat, provider resolution |
| `tests/cron/test_list_paging.cpp` | Paging, filtering, sorting for list/runs |
| `tests/agent/test_stop_phrases.cpp` | Multilingual stop phrase matching |
| `tests/infra/test_security_audit.cpp` | Multi-user heuristic detection |
| `tests/infra/test_sandbox_paths.cpp` | Hardlink detection, @-prefix normalization |
| `tests/infra/test_sandbox_network.cpp` | Network mode blocking, break-glass override |
| `tests/infra/test_exec_safety.cpp` | Shell wrapper detection, depth cap |
| `tests/infra/test_exec_trust.cpp` | Safe-bin risk classification |
| `tests/routing/test_turn_source.cpp` | Turn-source pinning |

## v2026.2.22

Port of [OpenClaw v2026.2.22](https://github.com/openclaw/openclaw) changes to C++23.

### Security Fixes

- **Cross-origin header stripping** — `FetchGuard::strip_cross_origin_headers()` removes `Authorization`, `Cookie`, and `Proxy-Authorization` when redirects cross origin boundaries.
- **HTML content sanitization** — `FetchGuard::sanitize_html_content()` strips hidden elements (`display:none`, `visibility:hidden`, `sr-only`, `aria-hidden`, `opacity:0`, `font-size:0`) to prevent indirect prompt injection.
- **Credential redaction** — `redact_credentials()` masks API keys, Bearer tokens, and `sk-`/`pk-` prefixed secrets in session histories. `strip_inbound_metadata()` removes `<!-- metadata:...-->` blocks.
- **Avatar security** — `GatewayServer::validate_avatar_path()` enforces canonical containment, rejects symlinks outside root, and enforces 2MB size limit.
- **Media download limits** — `kMaxMediaDownloadBytes` (50MB) enforced across Telegram, Discord, Slack, and WhatsApp channels. Oversized attachments are skipped with a warning.
- **WhatsApp allowFrom** — Outbound messages checked against configurable allowlist before delivery.
- **CLI config redaction** — `redact_config_json()` recursively masks `api_key`, `bot_token`, `access_token`, `token`, `secret`, `signing_secret` values in config output.
- **Telegram webhook secret** — `TelegramConfig::webhook_secret` field for `X-Telegram-Bot-Api-Secret-Token` validation.
- **Gateway markup sanitization** — Outbound WebSocket text frames stripped of `<script>` tags, event handlers, and `javascript:` URIs.

### New Providers

- **Mistral** — OpenAI-compatible API at `https://api.mistral.ai`. Models: `mistral-large-latest`, `mistral-medium-latest`, `mistral-small-latest`, `codestral-latest`, `open-mistral-nemo`, `mistral-embed`. Tool call ID sanitized to strict9 format.
- **Volcano Engine (Doubao/BytePlus)** — OpenAI-compatible at `https://ark.cn-beijing.volces.com/api/v3`. Uses endpoint IDs as model names.
- **Mistral Embeddings** — `MistralEmbeddings` class with `mistral-embed` model (1024 dimensions).
- **Gemini 3.1** — Added `gemini-3.1-pro-preview`, `gemini-3.1-pro-preview-antigravity-high`, `gemini-3.1-pro-preview-antigravity-low` to model catalog.

### Cron Fixes

- **Manual run** — `CronScheduler::manual_run()` triggers immediate execution with run-log tracking.
- **Abort support** — `abort_current()` sets atomic flag checked before each task execution.
- **Job ID sanitization** — Strips `..`, `/`, `\` from task names to prevent path traversal.
- **Run log cleanup** — `clean_run_log()` prunes completed entries.

### Compaction & Token Handling

- **Compaction counter** — `SessionData::auto_compaction_count` tracks completed compactions.
- **Compaction floor** — `SessionConfig::compaction_floor_tokens` enforces minimum token retention.

### Memory Improvements

- **Embedding hard-cap** — Input texts truncated to 32,000 chars (~8,000 tokens) before embedding API call.
- **Reindex on change** — `MemoryManager::reindex()` re-embeds content when source hash changes.
- **Multi-language stop-words** — BM25 preprocessing filters stop words for English, Spanish, Portuguese, Japanese, Korean, and Arabic.

### Channel Improvements

- **Discord DM scoping** — `_is_dm` flag tracks DM vs server channels for done-reply scoping.
- **Slack thread context** — `SlackConfig::reply_to_mode` ("thread"/"channel"/"auto") controls thread reply behavior. Thread session tracking via `thread_sessions_` map.
- **Telegram channel_post** — `channel_post` and `edited_channel_post` update types handled. Media group ID propagated via `_media_group_id`.
- **Per-channel model overrides** — `Config::model_by_channel` maps channel names to model IDs.

### Gateway & CLI

- **Auth unification** — `CredentialResolver` with defined precedence: Authorization header > ?token= > cookie > Tailscale.
- **OSC 8 hyperlinks** — CLI wraps detected URLs with terminal hyperlink escape sequences.

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
