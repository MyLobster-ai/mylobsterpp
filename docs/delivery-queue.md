# Write-Ahead Delivery Queue

The delivery queue provides reliable outbound message delivery with persistence, retry, and crash recovery.

## Architecture

```
Channel.send()
      │
      ▼
DeliveredChannel (wrapper)
      │
      ├─ run message_sending hook (can cancel or modify)
      ├─ enqueue to disk
      ├─ call inner channel send()
      ├─ ack on success / fail on error
      └─ run message_sent hook
```

`DeliveredChannel` wraps any `Channel` implementation using the decorator pattern. It delegates `start()`, `stop()`, `name()`, `type()`, and `is_running()` directly. Only `send()` is intercepted to add queue and hook integration.

## Persistence

Queue entries are stored as JSON files at `~/.openclaw/delivery-queue/{uuid}.json`.

```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "enqueued_at": "2026-02-14T12:00:00Z",
  "channel": "telegram",
  "to": "user123",
  "account_id": "acc_456",
  "payloads": [
    {
      "text": "Hello!",
      "attachments": [],
      "extra": {}
    }
  ],
  "retry_count": 0,
  "last_error": ""
}
```

### Atomic Writes

Entries are written to a `.tmp` file first, then atomically renamed via `std::filesystem::rename()`. This prevents partial writes from corrupting queue state on crash.

### Failed Entries

After max retries (5), entries are moved to `~/.openclaw/delivery-queue/failed/` for manual inspection.

## Retry Schedule

| Retry | Delay |
|-------|-------|
| 1 | 5 seconds |
| 2 | 25 seconds |
| 3 | 2 minutes |
| 4 | 10 minutes |
| 5 | (moved to failed/) |

Backoff values are defined in `kBackoffSeconds = {5, 25, 120, 600}`.

## Crash Recovery

On gateway startup, `recover_pending_deliveries()` is called:

1. Load all pending `.json` files from the queue directory
2. Sort by `enqueued_at` (oldest first)
3. For each entry: compute backoff delay from `retry_count`, re-attempt send via channel registry
4. Ack on success, fail (increment retry) on error
5. 60-second wall-clock deadline -- remaining entries are left for the next recovery cycle

## Hooks

### message_sending (pre-send)

Runs before the inner channel `send()`. The hook receives the outgoing message context and can:

- Return `{"cancel": true}` to skip the send entirely
- Return `{"content": "modified text"}` to replace the message content
- Return nothing to proceed unchanged

### message_sent (post-send)

Runs after send succeeds or fails. Receives the delivery result for logging, analytics, or follow-up actions.

## API

```cpp
#include "openclaw/infra/delivery_queue.hpp"

using namespace openclaw::infra;

DeliveryQueue queue("/path/to/queue/dir");

// Enqueue a delivery
auto id = queue.enqueue(delivery);  // Result<std::string>

// Acknowledge successful delivery
queue.ack(id.value());              // Result<void>

// Mark as failed (increments retry_count)
queue.fail(id.value(), "timeout");  // Result<void>

// Load all pending entries (sorted by enqueued_at)
auto pending = queue.load_pending(); // std::vector<QueuedDelivery>
```
