# GitHub Copilot OAuth Integration Plan

This document details the architectural design and step-by-step plan for implementing GitHub Copilot OAuth Device Flow authentication and token management within the Turbostar editor.

---

## 1. Architectural Overview

GitHub Copilot utilizes GitHub's OAuth 2.0 Device Authorization Grant (Device Flow). This is highly suited for terminal editors (like Turbostar) because it does not require spawning a local redirect HTTP web server on the user's host machine.

```mermaid
sequenceDiagram
    autonumber
    participant TUI as Turbostar TUI
    participant Config as Config Manager
    participant GH as GitHub OAuth API
    participant CP as Copilot API

    TUI->>GH: POST https://github.com/login/device/code
    GH-->>TUI: user_code, verification_uri, device_code, interval
    Note over TUI: Open Dialog & Display User Code
    loop Polling (every X seconds)
        TUI->>GH: POST https://github.com/login/oauth/access_token
    end
    Note over User: User visits page & authorizes
    GH-->>TUI: github_access_token
    TUI->>Config: Save github_access_token (models.json)
    Note over TUI: Close Dialog

    rect rgb(200, 220, 240)
        Note over TUI, CP: Token Lifecycle (Every 25 minutes)
        TUI->>CP: GET https://api.github.com/copilot_internal/v2/token
        CP-->>TUI: copilot_token, expires_at
        Note over TUI: Cache copilot_token
    end

    Note over TUI: Make Chat/Completions Calls
    TUI->>CP: POST https://api.githubcopilot.com/chat/completions (with Bearer copilot_token)
```

---

## 2. API Endpoints & Payloads

### Step A: Request Device Verification Code
* **Endpoint:** `POST https://github.com/login/device/code`
* **Headers:** 
  * `Accept: application/json`
* **Form Parameters:**
  * `client_id`: `Iv1.b507a08cbb0cc2c4` (Standard GitHub Copilot Client ID. This is required because GitHub restricts copilot-scoped tokens to official Client IDs.)
  * `scope`: `read:user`
* **Response Payload (JSON):**
  ```json
  {
    "device_code": "3584d835305513744c723226864861bff2d418e2",
    "user_code": "WDJB-MJHT",
    "verification_uri": "https://github.com/login/device",
    "expires_in": 900,
    "interval": 5
  }
  ```

### Step B: Poll for Access Token
* **Endpoint:** `POST https://github.com/login/oauth/access_token`
* **Headers:**
  * `Accept: application/json`
* **Form Parameters:**
  * `client_id`: `<client_id>`
  * `device_code`: `<device_code>`
  * `grant_type`: `urn:ietf:params:oauth:grant-type:device_code`
* **Response (Pending Authentication):**
  ```json
  {
    "error": "authorization_pending"
  }
  ```
* **Response (Successful Authentication):**
  ```json
  {
    "access_token": "gho_16C7ab42...",
    "token_type": "bearer",
    "scope": "read:user"
  }
  ```

### Step C: Retrieve Copilot Token
* **Endpoint:** `GET https://api.github.com/copilot_internal/v2/token`
* **Headers:**
  * `Authorization: token <access_token>`
  * `User-Agent: GithubCopilot/1.250.0`
* **Response Payload (JSON):**
  ```json
  {
    "token": "tid=3a9b5f...;exp=1781050409;...",
    "expires_at": 1781050409,
    "refresh_in": 1500
  }
  ```

---

## 3. Class Design & Code Changes

### A. Configuration Schema ([models.json](file:///home/arjan/git/turbostar2/src/agentlib/ai_model.cpp))
We will add fields to store the persistent GitHub Token and cache the short-lived Copilot token:
```json
{
  "id": "github-copilot",
  "name": "GitHub Copilot",
  "url": "https://api.githubcopilot.com",
  "type": "copilot",
  "github_access_token": "gho_...",
  "cached_copilot_token": "tid=...",
  "copilot_token_expires_at": 1781050409
}
```

### B. Copilot Authentication Manager
Create a dedicated manager `copilot_manager` inside `src/agentlib/` to handle token fetching and scheduling refreshes.

```cpp
// src/agentlib/copilot_manager.h
#pragma once
#include <string>
#include <mutex>
#include <chrono>

class copilot_manager {
public:
    static copilot_manager& get_instance();

    // Device Flow Trigger
    bool start_device_flow(std::string& user_code, std::string& verification_uri);
    bool poll_device_authorization(int interval_seconds);

    // Token retrieval
    std::string get_copilot_token();

private:
    copilot_manager() = default;
    
    std::string github_access_token_;
    std::string cached_copilot_token_;
    std::chrono::system_clock::time_point expires_at_;
    std::string device_code_;
    
    /* Mutex protecting shared cached token state */
    std::mutex token_mutex_;
};
```

### C. TUI Authentication Dialog ([dialog_factories.cpp](file:///home/arjan/git/turbostar2/src/ui/dialog_factories.cpp))
Implement a visual dialog factory method `show_copilot_auth_dialog` that:
1. Triggers `start_device_flow()`.
2. Spawns a background worker thread calling `poll_device_authorization()`.
3. Renders a popup box in the terminal displaying the 8-character verification code (`user_code`) and the login URL (`https://github.com/login/device`). The user can copy the URL or type it in their browser manually. No automatic browser opening is performed.
4. Cleans up and closes automatically on success or failure.

---

## 4. Implementation Steps

| Step | Action | Files Modified |
| :--- | :--- | :--- |
| **1** | Define `copilot` API type and config attributes. | [ai_model.h](file:///home/arjan/git/turbostar2/src/agentlib/ai_model.h), [ai_model.cpp](file:///home/arjan/git/turbostar2/src/agentlib/ai_model.cpp) |
| **2** | Implement `copilot_manager` REST handlers. | `src/agentlib/copilot_manager.h`, `src/agentlib/copilot_manager.cpp` |
| **3** | Update transport layer to inject Copilot Bearer token. | [httplib_transport.cpp](file:///home/arjan/git/turbostar2/src/agentlib/httplib_transport.cpp) |
| **4** | Build the Device Auth Dialog. | [dialog_factories.h](file:///home/arjan/git/turbostar2/src/ui/dialog_factories.h), [dialog_factories.cpp](file:///home/arjan/git/turbostar2/src/ui/dialog_factories.cpp) |
| **5** | Add GTest unit testing covering OAuth mocked states. | `tests/unit/test_copilot_auth.cpp` |
