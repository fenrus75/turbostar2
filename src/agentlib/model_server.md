# Model Server (`model_server`)

## Goals
The `model_server` class represents an external LLM endpoint host (e.g., an OpenAI API server, local Ollama instance, vLLM cluster, or corporate gateway). It consolidates base endpoint URLs, authorization credentials, protocol types, and baseline latency/preference scoring across multiple individual models.

## Architecture and Constraints
- **Endpoint Abstraction**:
  - Encapsulates `id`, `name`, `url`, `api_key`, `api_type`, and `base_score`.
  - Multiple `ai_model` definitions can reference a single `model_server` via `server_id`, inheriting endpoint settings while overriding context sizes and pricing rates.
- **Model Server Registry (`model_server_registry`)**:
  - Singleton managing configured server instances (`servers_`).
  - Serializes to and loads from `~/.turbostar/servers.json`.
  - Provides thread-safe lookup, registration, modification, and deletion.

## Lessons Learned
- **Centralizing Credentials**: Storing API keys per model caused severe maintenance headaches when rotating credentials or updating proxy URLs across dozens of models. Factoring out `model_server` as a shared parent entity eliminated credential duplication.
- **Base Scoring**: Providing a configurable `base_score` on model servers allows local on-premise servers (e.g. Ollama) or cost-effective proxies to be prioritized automatically by heuristic model selectors.
