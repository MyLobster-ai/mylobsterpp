#include "openclaw/gateway/tts_handler.hpp"

#include <boost/asio/use_awaitable.hpp>

#include "openclaw/core/logger.hpp"

namespace openclaw::gateway {

using json = nlohmann::json;
using boost::asio::awaitable;

void register_tts_handlers(Protocol& protocol) {
    // tts.status — get text-to-speech service status.
    protocol.register_method("tts.status",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"enabled", false},
                {"provider", "none"},
                {"voice", ""},
            };
        },
        "Get TTS service status", "tts");

    // tts.enable — enable text-to-speech.
    protocol.register_method("tts.enable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto provider = params.value("provider", "edge");
            auto voice = params.value("voice", "");
            LOG_INFO("TTS enabled: provider={}, voice={}", provider, voice);
            co_return json{{"ok", true}, {"provider", provider}, {"voice", voice}};
        },
        "Enable text-to-speech", "tts");

    // tts.disable — disable text-to-speech.
    protocol.register_method("tts.disable",
        []([[maybe_unused]] json params) -> awaitable<json> {
            LOG_INFO("TTS disabled");
            co_return json{{"ok", true}};
        },
        "Disable text-to-speech", "tts");

    // tts.convert — convert text to speech audio.
    protocol.register_method("tts.convert",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto text = params.value("text", "");
            if (text.empty()) {
                co_return json{{"ok", false}, {"error", "text is required"}};
            }
            auto provider = params.value("provider", "edge");
            auto voice = params.value("voice", "");
            auto format = params.value("format", "mp3");
            // In a full implementation, this would call Edge TTS or ElevenLabs
            // and return base64-encoded audio data.
            co_return json{
                {"ok", false},
                {"error", "TTS conversion requires provider integration"},
                {"provider", provider},
            };
        },
        "Convert text to speech audio", "tts");

    // tts.setProvider — set the TTS provider.
    protocol.register_method("tts.setProvider",
        []([[maybe_unused]] json params) -> awaitable<json> {
            auto provider = params.value("provider", "");
            if (provider.empty()) {
                co_return json{{"ok", false}, {"error", "provider is required"}};
            }
            co_return json{{"ok", true}, {"provider", provider}};
        },
        "Set TTS provider", "tts");

    // tts.providers — list available TTS providers.
    protocol.register_method("tts.providers",
        []([[maybe_unused]] json params) -> awaitable<json> {
            co_return json{
                {"ok", true},
                {"providers", json::array({
                    json{{"name", "edge"}, {"displayName", "Microsoft Edge TTS"}, {"available", true}},
                    json{{"name", "elevenlabs"}, {"displayName", "ElevenLabs"}, {"available", false}},
                })},
            };
        },
        "List available TTS providers", "tts");

    LOG_INFO("Registered TTS handlers");
}

} // namespace openclaw::gateway
