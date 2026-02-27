#include "openclaw/gateway/chat_handler.hpp"

#include <atomic>
#include <deque>
#include <mutex>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

namespace {

static std::atomic<uint64_t> run_counter{0};

auto generate_run_id() -> std::string {
    auto count = run_counter.fetch_add(1, std::memory_order_relaxed);
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "run-" + std::to_string(ms) + "-" + std::to_string(count);
}

/// Thread-safe chunk queue shared between the StreamCallback (background
/// thread) and the consumer coroutine (asio thread).
struct ChunkQueue {
    std::mutex mtx;
    std::deque<providers::CompletionChunk> chunks;
    bool done = false;
};

/// Consumer coroutine: drains chunks from the queue and broadcasts each
/// one in real-time over WebSocket. Runs on the asio thread.
auto consume_chunks(std::shared_ptr<ChunkQueue> queue,
                    std::shared_ptr<boost::asio::steady_timer> timer,
                    std::string run_id,
                    GatewayServer& server) -> awaitable<void> {
    for (;;) {
        // Wait for notification (timer cancel) from the producer.
        boost::system::error_code ec;
        co_await timer->async_wait(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        // Drain all available chunks.
        std::deque<providers::CompletionChunk> batch;
        bool finished = false;
        {
            std::lock_guard lock(queue->mtx);
            batch.swap(queue->chunks);
            finished = queue->done;
        }

        for (const auto& chunk : batch) {
            if (chunk.type == "text") {
                co_await server.broadcast(make_event("chat", json{
                    {"runId", run_id},
                    {"state", "delta"},
                    {"stream", "assistant"},
                    {"text", chunk.text},
                }));
            } else if (chunk.type == "tool_use") {
                co_await server.broadcast(make_event("agent", json{
                    {"runId", run_id},
                    {"stream", "tool"},
                    {"toolName", chunk.tool_name.value_or("")},
                    {"toolInput", chunk.tool_input.value_or(json::object())},
                }));
            } else if (chunk.type == "thinking") {
                co_await server.broadcast(make_event("agent", json{
                    {"runId", run_id},
                    {"stream", "thinking"},
                    {"text", chunk.text},
                }));
            }
        }

        if (finished) {
            co_return;
        }

        // Reset timer for next notification.
        timer->expires_at(boost::asio::steady_timer::time_point::max());
    }
}

/// Detached coroutine that performs the actual AI completion and streams
/// events back via broadcast in real-time. Runs after the ack has been sent.
auto run_chat_completion(std::string run_id,
                         std::string message_text,
                         GatewayServer& server,
                         agent::AgentRuntime& runtime) -> awaitable<void> {
    auto executor = co_await boost::asio::this_coro::executor;

    // Set up thread-safe queue and timer for real-time streaming.
    // Created outside try/catch so cleanup always happens.
    auto queue = std::make_shared<ChunkQueue>();
    auto timer = std::make_shared<boost::asio::steady_timer>(
        executor, boost::asio::steady_timer::time_point::max());

    // Spawn consumer coroutine that broadcasts chunks as they arrive.
    auto consumer = boost::asio::co_spawn(executor,
        consume_chunks(queue, timer, run_id, server),
        boost::asio::use_awaitable);

    std::string error_msg;
    try {
        providers::CompletionRequest req;
        if (auto provider = runtime.provider(); provider) {
            auto models = provider->models();
            if (!models.empty()) {
                req.model = models[0];
            }
        }

        LOG_INFO("chat.send run={} model={} msg_len={}",
                 run_id, req.model, message_text.size());

        // Add the user message.
        Message user_msg;
        user_msg.role = Role::User;
        user_msg.content.push_back(ContentBlock{.type = "text", .text = message_text});
        user_msg.created_at = Clock::now();
        req.messages.push_back(std::move(user_msg));

        // Include tool definitions from the runtime's tool registry.
        req.tools = runtime.tool_registry().to_anthropic_json();

        // StreamCallback: pushes each chunk to the queue and notifies
        // the consumer. Runs on the background thread (from post_stream).
        auto stream_cb = [queue, timer](const providers::CompletionChunk& chunk) {
            {
                std::lock_guard lock(queue->mtx);
                queue->chunks.push_back(chunk);
            }
            // Post cancel to the timer's executor for thread safety.
            boost::asio::post(timer->get_executor(),
                              [timer] { timer->cancel(); });
        };

        auto result = co_await runtime.process_with_tools_stream(
            std::move(req), stream_cb);

        // Signal consumer that streaming is done, then wait for it.
        {
            std::lock_guard lock(queue->mtx);
            queue->done = true;
        }
        timer->cancel();
        co_await std::move(consumer);

        // Send final or error event.
        if (result.has_value()) {
            auto& resp = result.value();
            std::string final_text;
            for (const auto& block : resp.message.content) {
                if (block.type == "text") {
                    final_text += block.text;
                }
            }
            LOG_INFO("chat.send run={} completed: {} chars, model={}",
                     run_id, final_text.size(), resp.model);
            co_await server.broadcast(make_event("chat", json{
                {"runId", run_id},
                {"state", "final"},
                {"text", final_text},
                {"model", resp.model},
                {"inputTokens", resp.input_tokens},
                {"outputTokens", resp.output_tokens},
                {"stopReason", resp.stop_reason},
            }));
        } else {
            LOG_ERROR("chat.send run={} provider error: {}",
                      run_id, result.error().what());
            co_await server.broadcast(make_event("chat", json{
                {"runId", run_id},
                {"state", "error"},
                {"error", result.error().what()},
            }));
        }

        co_return;
    } catch (const std::exception& e) {
        LOG_ERROR("chat.send run={} exception: {}", run_id, e.what());
        error_msg = std::string("Internal error: ") + e.what();
    } catch (...) {
        LOG_ERROR("chat.send run={} unknown exception", run_id);
        error_msg = "Internal error (unknown)";
    }

    // Exception path: shut down consumer and broadcast error.
    {
        std::lock_guard lock(queue->mtx);
        queue->done = true;
    }
    timer->cancel();
    co_await std::move(consumer);

    co_await server.broadcast(make_event("chat", json{
        {"runId", run_id},
        {"state", "error"},
        {"error", error_msg},
    }));
}

/// Core chat handler: returns ack immediately, spawns streaming work.
auto handle_chat_send(json params,
                      GatewayServer& server,
                      [[maybe_unused]] sessions::SessionManager& sessions,
                      agent::AgentRuntime& runtime) -> awaitable<json> {
    auto message_text = params.value("message", "");
    if (message_text.empty()) {
        co_return json{{"ok", false}, {"error", "message is required"}};
    }

    auto run_id = generate_run_id();
    auto executor = co_await boost::asio::this_coro::executor;

    // Spawn the completion work as a detached coroutine so the ack
    // returns to the client immediately.
    boost::asio::co_spawn(executor,
        run_chat_completion(run_id, std::move(message_text), server, runtime),
        boost::asio::detached);

    co_return json{{"runId", run_id}};
}

} // anonymous namespace

void register_chat_handlers(Protocol& protocol,
                            GatewayServer& server,
                            sessions::SessionManager& sessions,
                            agent::AgentRuntime& runtime) {
    // chat.send — primary method called by bridge for every user message.
    protocol.register_method("chat.send",
        [&server, &sessions, &runtime](json params) -> awaitable<json> {
            co_return co_await handle_chat_send(
                std::move(params), server, sessions, runtime);
        },
        "Send a chat message and receive streaming response", "chat");

    // agent.chat — alias for chat.send.
    protocol.register_method("agent.chat",
        [&server, &sessions, &runtime](json params) -> awaitable<json> {
            co_return co_await handle_chat_send(
                std::move(params), server, sessions, runtime);
        },
        "Send a message to the agent and get a response", "agent");

    // agent.chat.stream — explicit streaming variant (same behavior).
    protocol.register_method("agent.chat.stream",
        [&server, &sessions, &runtime](json params) -> awaitable<json> {
            co_return co_await handle_chat_send(
                std::move(params), server, sessions, runtime);
        },
        "Stream agent chat response", "agent");

    LOG_INFO("Registered chat handlers: chat.send, agent.chat, agent.chat.stream");
}

} // namespace openclaw::gateway
