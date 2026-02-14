# Providers

MyLobster++ supports multiple LLM providers through a unified `Provider` interface. Each provider implements `complete()` (single response), `stream()` (SSE/NDJSON streaming), `name()`, and `models()`.

## Provider List

| Provider | API Format | Base URL | Auth |
|----------|-----------|----------|------|
| Anthropic | Anthropic Messages API | `https://api.anthropic.com/v1` | `x-api-key` header |
| OpenAI | OpenAI Chat Completions | `https://api.openai.com/v1` | `Authorization: Bearer` |
| Gemini | Google AI Studio | `https://generativelanguage.googleapis.com` | API key in URL |
| Bedrock | AWS Bedrock | Regional AWS endpoint | AWS Signature V4 |
| Hugging Face | OpenAI-compatible | `https://router.huggingface.co/v1` | `Authorization: Bearer` |
| Ollama | Ollama native | `http://127.0.0.1:11434` | None (local) |
| Synthetic | Anthropic-compatible | `https://api.synthetic.new/anthropic` | `x-api-key` header |

## Anthropic

The default provider. Connects to the Anthropic Messages API with SSE streaming.

```json
{
  "name": "anthropic",
  "api_key": "${ANTHROPIC_API_KEY}",
  "model": "claude-sonnet-4-20250514"
}
```

Supports thinking mode via the `ThinkingMode` configuration for extended reasoning.

## OpenAI

OpenAI Chat Completions API with SSE streaming. The configurable `base_url` enables compatibility with any OpenAI-format API (LM Studio, vLLM, text-generation-webui).

```json
{
  "name": "openai",
  "api_key": "${OPENAI_API_KEY}",
  "model": "gpt-4o",
  "base_url": "https://api.openai.com/v1"
}
```

Models include `gpt-4o`, `gpt-4o-mini`, `gpt-5.3-codex-spark`, and others.

## Hugging Face Inference

OpenAI-compatible endpoint routed through Hugging Face's inference router. Reuses the SSE parsing infrastructure from the OpenAI provider.

```json
{
  "name": "huggingface",
  "api_key": "${HUGGINGFACE_API_KEY}",
  "model": "meta-llama/Llama-3.3-70B-Instruct"
}
```

### Route Policy Suffixes

Append `:cheapest` or `:fastest` to model names to influence routing:

```json
{ "model": "meta-llama/Llama-3.3-70B-Instruct:cheapest" }
```

The suffix is stripped from the model name and passed as a routing hint header.

### Static Catalog

A built-in catalog of ~20 known models provides context length and max token defaults (131072 / 8192). Unknown models fall back to these defaults.

### Dynamic Discovery

On initialization, the provider queries `GET /v1/models` to discover available models. Network failures fall back to the static catalog silently.

### Reasoning Detection

Models with `r1`, `reasoning`, `thinking`, or `reason` in their ID are automatically flagged for extended reasoning support.

## Ollama

Native provider for local Ollama instances. Uses NDJSON streaming (not SSE) -- each line is a standalone JSON object.

```json
{
  "name": "ollama",
  "base_url": "http://127.0.0.1:11434",
  "model": "llama3.2"
}
```

### NDJSON Streaming

Unlike SSE-based providers, Ollama sends responses as newline-delimited JSON:

```
{"model":"llama3.2","message":{"role":"assistant","content":"Hello"},"done":false}
{"model":"llama3.2","message":{"role":"assistant","content":" world"},"done":false}
{"model":"llama3.2","message":{"role":"assistant","content":""},"done":true}
```

### Tool Call Accumulation

Ollama sends partial `tool_calls` across multiple chunks. The provider accumulates them in a JSON object and finalizes when `"done": true`.

### Message Conversion

- `toolResult` role maps to Ollama's `tool` role
- Images are passed as base64 strings in an `"images"` array
- Request options: `num_ctx` (context), `num_predict` (max tokens), `temperature`

## Synthetic Catalog

Anthropic-compatible API providing access to 22 third-party models via `api.synthetic.new`.

```json
{
  "name": "synthetic",
  "api_key": "${SYNTHETIC_API_KEY}",
  "model": "deepseek-r1"
}
```

### Available Models

MiniMax M2.1, DeepSeek R1, DeepSeek V3, Qwen3, GLM-4.5, GLM-4.6, GLM-5, Llama 3.3, Llama 4, Kimi K2, and others. Full catalog in `src/providers/synthetic.cpp`.

### HF Prefix Resolution

Models with `hf:` prefix (e.g., `hf:deepseek-ai/DeepSeek-R1`) are resolved to API identifiers via `resolve_hf_model()`.

## Gemini

Google AI Studio API with streaming support.

```json
{
  "name": "gemini",
  "api_key": "${GEMINI_API_KEY}",
  "model": "gemini-2.0-flash"
}
```

## Bedrock

AWS Bedrock with Signature V4 authentication. Requires AWS credentials in environment.

```json
{
  "name": "bedrock",
  "region": "us-east-1",
  "model": "anthropic.claude-sonnet-4-20250514-v1:0"
}
```

## Adding a Custom Provider

Derive from `Provider` and implement the virtual interface:

```cpp
#include "openclaw/providers/provider.hpp"

class MyProvider : public openclaw::providers::Provider {
public:
    MyProvider(boost::asio::io_context& ioc, const ProviderConfig& config);

    auto complete(const CompletionRequest& req)
        -> boost::asio::awaitable<Result<CompletionResponse>> override;

    auto stream(const CompletionRequest& req,
                StreamCallback callback)
        -> boost::asio::awaitable<Result<void>> override;

    auto name() const -> std::string override { return "my_provider"; }
    auto models() const -> std::vector<ModelInfo> override;
};
```

Register the provider in `src/core/config.cpp` by adding a name match in the provider factory.
