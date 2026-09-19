# AI Model (`ai_model`)

## Goals
The `ai_model` class represents an individual language model configuration in Turbostar, encapsulating its API protocol type, pricing structure, token window capacities, endpoint URL, and capability flags (vision, coding, video, audio).

## Architecture and Constraints
- **Protocol Formats**: Supports diverse model API types via `api_type`:
  - `openai`: Standard OpenAI chat completion format (`/v1/chat/completions`).
  - `openai_response`: Non-mutating stateful response format (`/v1/responses`).
  - `gemini`: Google Gemini REST protocol (`generateContent`).
  - `claude`: Anthropic Messages protocol.
  - `copilot`: GitHub Copilot chat format with short-lived session tokens.
- **Model Server Association**: Models can map to a parent `model_server` via `server_id`, inheriting base endpoints and credentials while maintaining model-specific parameters.
- **Cost & Token Accounting**: Tracks input (`cost_per_1m_tx`) and output (`cost_per_1m_rx`) token rates to update running financial metrics in `ai_agent` after each turn.
- **Registry**: `ai_model_registry` manages registered model configurations, providing lookup, discovery, and serialization to `models.json`.

## Lessons Learned
- **Credential Inheritance**: Storing API keys directly on each model caused configuration duplication when multiple models shared an endpoint. Linking models to `model_server` records eliminates redundant updates when keys rotate.
- **Protocol Capabilities**: Models vary in their support for system prompts, parallel tool calling, and thinking tags. Checking capability flags before formatting payloads prevents HTTP 400 rejection errors.
