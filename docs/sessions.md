# Sessions

The sessions module manages conversation state, user identity, and persistence.

## Session Structure

```cpp
struct Session {
    std::string id;
    std::string user_id;
    std::string device_id;
    std::optional<std::string> channel;
    std::optional<std::string> agent_id;   // Multi-agent routing
    int64_t created_at;
    int64_t last_active;

    // Generate session key for lookups
    std::string session_key(const std::string& peer_id) const;
};
```

## Session Keys

Sessions are identified by a composite key for multi-agent routing:

```
{agent_id}:{channel}:{peer_id}
```

Examples:
- `agent1:telegram:user123` — Agent "agent1", Telegram channel, user "user123"
- `:discord:user456` — No specific agent, Discord channel, user "user456"

When `agent_id` is empty, the leading colon is preserved for consistent parsing.

## Session States

```cpp
enum class SessionState {
    Active,   // Currently in use
    Idle,     // No recent activity
    Closed    // Explicitly ended
};
```

## SQLite Store

Sessions are persisted in SQLite with WAL journal mode:

### Schema

```sql
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    device_id TEXT NOT NULL,
    channel TEXT,
    agent_id TEXT,
    state TEXT NOT NULL DEFAULT 'active',
    metadata TEXT DEFAULT '{}',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_state ON sessions(state);
CREATE INDEX IF NOT EXISTS idx_sessions_agent_id ON sessions(agent_id);
```

### Migration

Existing databases without the `agent_id` column are automatically migrated:

```cpp
// Attempted on startup; silently succeeds if column exists
ALTER TABLE sessions ADD COLUMN agent_id TEXT;
CREATE INDEX IF NOT EXISTS idx_sessions_agent_id ON sessions(agent_id);
```

## Store API

```cpp
#include "openclaw/sessions/store.hpp"

using namespace openclaw::sessions;

SqliteSessionStore store(ioc, "/path/to/sessions.db");

// Create a session
SessionData data;
data.session.id = generate_uuid();
data.session.user_id = "user123";
data.session.agent_id = "agent1";
data.state = SessionState::Active;
auto result = co_await store.create(data);

// Get by ID
auto session = co_await store.get("session-id");

// List user sessions
auto sessions = co_await store.list("user123");

// Update
data.state = SessionState::Idle;
co_await store.update(data);

// Remove expired
co_await store.remove_expired(86400);  // TTL in seconds

// Remove by ID
co_await store.remove("session-id");
```

## Session Manager

The `SessionManager` provides higher-level session lifecycle management:

- Creates sessions with auto-generated IDs
- Updates `last_active` timestamp on activity
- Handles TTL-based cleanup
- Archives transcripts before deletion (moved to `{data_dir}/transcripts/archive/`)

## agentId Routing

The `agent_id` field enables multi-agent deployments where different agents handle different contexts:

```json
{
  "sessions": {
    "store": "sqlite",
    "ttl_seconds": 86400
  }
}
```

When routing incoming messages, the session store queries by both `agent_id` and channel to find the correct session context. This prevents cross-talk between agents sharing the same database.
