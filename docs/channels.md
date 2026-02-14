# Channels

Channels provide the messaging interface between users and the AI agent. Each channel implements the `Channel` base class with `start()`, `stop()`, `send()`, `name()`, `type()`, and `is_running()` methods.

## Supported Channels

| Channel | Protocol | Features |
|---------|----------|----------|
| Telegram | Bot API (HTTPS polling) | Voice routing, 100-command menu, bot commands |
| Discord | Gateway WebSocket + REST | Voice messages, presence, auto-threading |
| Slack | Events API + Web API | Thread-ownership gating, @-mention bypass |
| WhatsApp | Cloud API | Document filename preservation, media messages |
| Signal | signal-cli (subprocess) | arm64 detection, group messaging |
| LINE | Messaging API | Webhooks, rich messages |

## Discord

### Voice Messages

Send audio as voice messages with waveform visualization:

```
1. Request upload URL: POST /channels/{id}/attachments
2. Upload file: PUT to CDN URL
3. Send message: POST /channels/{id}/messages
   - flags: kVoiceMessageFlag (1 << 13) | kSuppressNotificationsFlag (1 << 12)
   - attachments[0].waveform: base64-encoded 256 amplitude samples
```

Waveform generation converts 16-bit signed LE PCM to 256 amplitude samples (0-255), then base64-encodes the result.

### Configurable Presence

Set bot presence in the Discord gateway IDENTIFY payload:

```json
{
  "channels": [{
    "type": "discord",
    "settings": {
      "bot_token": "...",
      "presence_status": "online",
      "activity_name": "Helping users",
      "activity_type": 0,
      "activity_url": ""
    }
  }]
}
```

Activity types: 0 = Playing, 1 = Streaming, 2 = Listening, 3 = Watching, 5 = Competing.

### AutoThread Reply Routing

Automatically create threads for replies:

```json
{
  "settings": {
    "auto_thread": true,
    "auto_thread_ttl_minutes": 10
  }
}
```

When enabled, replies to non-thread messages automatically create a new thread via `POST /channels/{id}/messages/{msg}/threads`. Thread names are truncated to 100 characters.

## Telegram

### Voice Routing

Audio files with compatible extensions are sent via `sendVoice` instead of `sendDocument`:

| Extension | Compatible |
|-----------|-----------|
| `.mp3` | Yes |
| `.m4a` | Yes |
| `.ogg` | Yes |
| `.oga` | Yes |
| `.opus` | Yes |
| `.wav` | No |
| `.flac` | No |

Detection is case-insensitive via `is_voice_compatible()`.

### Bot Menu Commands

`build_capped_menu_commands()` validates and caps the bot command menu:

1. Filter: regex `[a-z0-9_]{1,32}` (lowercase alphanumeric + underscore, max 32 chars)
2. Deduplicate: first occurrence wins
3. Cap at 100 (Telegram API limit)
4. Log warning on overflow: "Registered {N} of {total} commands (Telegram limit: 100)"

## Slack

### Thread-Ownership Gating

Before sending to a thread, the channel checks for `@<BOT_USER_ID>` mentions. If the message text contains an @-mention of the bot, the gating check is bypassed and the message is sent directly.

## WhatsApp

### Document Filename Preservation

When sending document-type media messages, the original `filename` from `OutgoingMessage::attachments` is propagated to the Cloud API `media_object`. This preserves user-friendly filenames instead of defaulting to content-hash names.

## Signal

### arm64 Detection

On arm64 Linux, a compile-time warning is emitted noting that `signal-cli` requires manual installation:

```
[WARN] arm64 Linux detected. signal-cli may need manual installation.
       Install via: brew install signal-cli (Homebrew on Linux)
       Or download from: https://github.com/AsamK/signal-cli/releases
```

## DeliveredChannel Wrapper

All channels can be wrapped with `DeliveredChannel` for reliable delivery:

```cpp
auto inner = std::make_unique<TelegramChannel>(ioc, config);
auto channel = std::make_unique<DeliveredChannel>(
    std::move(inner), queue, hooks);
```

The wrapper adds write-ahead queuing and hook integration without modifying channel implementations. See [delivery-queue.md](delivery-queue.md) for details.
