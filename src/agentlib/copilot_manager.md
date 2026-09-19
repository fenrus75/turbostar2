# Copilot Manager (`copilot_manager`)

## Goals
The `copilot_manager` class handles authentication, authorization, token lifecycles, and model discovery for GitHub Copilot. It enables Turbostar users to use their GitHub Copilot subscriptions seamlessly within the editor.

## Architecture and Constraints
- **OAuth Device Flow**: Implements the GitHub OAuth Device Authorization Grant (`start_device_flow`, `poll_device_authorization`), returning verification URLs and user codes for terminal/modal authentication.
- **Short-Lived Token Exchange**: Exchanges the persistent GitHub OAuth token for short-lived Copilot session tokens (`get_copilot_token`), automatically refreshing them before expiration (`expires_at_`).
- **Model Catalog Discovery**: Queries the GitHub Copilot model catalog (`fetch_and_register_github_models`), parses available model identifiers, and registers them dynamically into `ai_model_registry`.
- **Lock Ordering & Concurrency**:
  - `token_mutex_` protects token state and polling intervals.
  - **Critical Rule**: The mutex is acquired briefly to check or update state and released before executing blocking network I/O (libcurl requests) to prevent deadlocking callers.

## Lessons Learned
- **Lock Scoping Around HTTP Requests**: Holding `token_mutex_` across `perform_curl_request()` caused UI thread hangs whenever network latency spiked. Always release state mutexes before initiating external network requests.
- **Clock Drift Safety**: Copilot session tokens typically expire in 30 minutes; checking expiration with a 5-minute safety margin prevents in-flight 401 Unauthorized errors during long tool execution chains.
