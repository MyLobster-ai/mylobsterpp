# Routing & Scope Rules

The routing module controls how incoming messages are matched to handlers based on rules.

## Binding Scopes

Messages can be scoped to different levels of granularity:

| Scope | Description | Matches On |
|-------|-------------|-----------|
| `Peer` | Single user | `BindingContext::peer_id` |
| `Guild` | Discord server / group | `BindingContext::guild_id` |
| `Team` | Slack workspace / team | `BindingContext::team_id` |
| `Global` | Everything | Always matches |

## BindingContext

Channels populate `BindingContext` on incoming messages:

```cpp
struct BindingContext {
    std::string peer_id;                    // Always present
    std::optional<std::string> guild_id;    // Discord servers
    std::optional<std::string> team_id;     // Slack workspaces
};
```

The context is stored as `std::optional<BindingContext>` on `IncomingMessage`. Messages without binding context only match `Global` scope rules.

## ScopeRule

`ScopeRule` matches messages against a specific scope and identifier:

```cpp
using namespace openclaw::routing;

// Match only messages from user123
ScopeRule peer_rule(BindingScope::Peer, "user123");

// Match all messages in guild789
ScopeRule guild_rule(BindingScope::Guild, "guild789");

// Match everything
ScopeRule global_rule(BindingScope::Global, "");
```

### Matching Logic

- **Peer**: Requires `binding.peer_id == rule.id()`
- **Guild**: Requires `binding.guild_id.has_value() && *binding.guild_id == rule.id()`
- **Team**: Requires `binding.team_id.has_value() && *binding.team_id == rule.id()`
- **Global**: Always returns `true`
- Messages without `BindingContext` only match Global rules

### Naming Convention

Each scope rule has a canonical name for logging and configuration:

```
scope:peer:user123
scope:guild:guild789
scope:team:team456
scope:global
```

## Session Keys with agentId

Sessions now support an optional `agent_id` for multi-agent routing:

```
{agent_id}:{channel}:{peer_id}
```

When `agent_id` is empty, the key format is `:{channel}:{peer_id}` (leading colon preserved for consistency).

## Configuration

Scope rules are typically configured in the routing section:

```json
{
  "routing": {
    "rules": [
      { "type": "scope", "scope": "guild", "id": "guild789" },
      { "type": "scope", "scope": "peer", "id": "user123" }
    ]
  }
}
```
