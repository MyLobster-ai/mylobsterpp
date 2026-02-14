# Cron Scheduler

The cron module provides time-based task scheduling with standard cron expression support.

## Cron Expressions

Standard 5-field cron format:

```
┌───────────── minute (0-59)
│ ┌───────────── hour (0-23)
│ │ ┌───────────── day of month (1-31)
│ │ │ ┌───────────── month (1-12)
│ │ │ │ ┌───────────── day of week (0-6, Sunday=0)
│ │ │ │ │
* * * * *
```

### Field Syntax

| Syntax | Meaning | Example |
|--------|---------|---------|
| `*` | Every value | `* * * * *` = every minute |
| `N` | Exact value | `30 * * * *` = at minute 30 |
| `N-M` | Range | `0-15 * * * *` = minutes 0 through 15 |
| `*/N` | Step | `*/5 * * * *` = every 5 minutes |
| `N,M` | List | `0,30 * * * *` = at minute 0 and 30 |

### Examples

```
* * * * *        Every minute
0 * * * *        Every hour (at minute 0)
0 0 * * *        Every day at midnight
0 9 * * 1-5      Weekdays at 9:00 AM
*/15 * * * *     Every 15 minutes
0 0 1 * *        First day of every month
```

## Scheduler

The scheduler ticks once per minute and fires matching tasks as detached coroutines.

```cpp
#include "openclaw/cron/scheduler.hpp"

using namespace openclaw::cron;

boost::asio::io_context ioc;
CronScheduler scheduler(ioc);

// Schedule a recurring task
auto result = scheduler.schedule("cleanup", "0 */6 * * *",
    []() -> boost::asio::awaitable<void> {
        // Runs every 6 hours
        co_return;
    });

// Schedule a one-shot task
auto result = scheduler.schedule("one_time", "30 14 * * *",
    []() -> boost::asio::awaitable<void> {
        // Runs once at 2:30 PM, then auto-deletes
        co_return;
    },
    true);  // delete_after_run = true

// Start the scheduler (blocks until stop)
co_await scheduler.start();
```

## deleteAfterRun

One-shot tasks are automatically removed after successful execution:

```cpp
scheduler.schedule("migration", "0 3 * * *",
    migration_task,
    true);  // delete_after_run = true
```

After the task fires successfully:
1. The task callback runs to completion
2. The scheduler acquires its internal lock
3. The task is removed from the task map
4. A log message confirms removal: "One-shot task 'migration' completed and removed"

If the task throws an exception, it is **not** removed and will fire again at the next matching time.

## Task Management

```cpp
// List registered tasks
auto names = scheduler.task_names();  // std::vector<std::string>

// Check count
auto count = scheduler.size();

// Cancel a specific task
auto result = scheduler.cancel("cleanup");  // Result<void>

// Check if running
bool running = scheduler.is_running();

// Stop the scheduler
scheduler.stop();
```

## Execution Model

1. Scheduler waits until the next minute boundary (+1 second buffer for clock skew)
2. Checks all registered tasks against the current time
3. Matching tasks are spawned as detached `co_spawn` coroutines
4. Tasks run concurrently and independently
5. Exceptions in tasks are caught and logged, but don't affect other tasks or the scheduler
6. One-shot tasks (`delete_after_run = true`) are removed after successful completion

## Configuration

Enable cron in the main config:

```json
{
  "cron": {
    "enabled": true
  }
}
```

Tasks are registered programmatically via the `CronScheduler` API. The config flag controls whether the scheduler is started during gateway initialization.
