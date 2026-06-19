# Agentlib Redesign Blueprint

The current architectural design is documented in `docs/design-agent.md`.

## Goals
Refactor `src/agentlib/` to introduce modular, decoupled interfaces. This makes it easier to support multiple API backends/protocols (e.g. stateful OpenAI, streaming Gemini, Copilot) and simplifies context management (compaction and paging).

## Core Architecture

### 1. Separation of Data and UI
To ensure `agentlib` remains fully decoupled from visual rendering details (and NCurses/TUI dependencies):
* **Data Layer:** Core data classes (`Turn`, `Episode`, `Conversation`) deal exclusively with standard C++ types and structure.
* **UI Layer:** Visual presentation is driven by standard **Markdown representations** exported by turns (`Turn::to_markdown()`). The rendering views (e.g., subclasses of `agent_interaction`) parse and style these Markdown blocks (handles box outlines, color themes, and code highlight overrides).

---

## Class Interfaces

### A. Turn (`agentlib::Turn`)
Represents a single logical element/transaction in the conversation.

```cpp
namespace agentlib {

enum class turn_type {
    system,
    user,
    model_response,
    tool_execution,
    error
};

class Turn {
public:
    virtual ~Turn() = default;

    virtual std::string get_id() const = 0;
    virtual turn_type get_type() const = 0;

    // Generates the raw messages sent to the LLM backend.
    // Respects the active compaction level of the parent episode.
    virtual std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const = 0;

    // UI View-Model Output: Generates a clean Markdown representation of the turn contents.
    virtual std::string to_markdown() const = 0;

    // Streaming updates: appends incremental token chunks (for text or reasoning streams).
    virtual void append_content(const std::string& chunk) = 0;

    // Serialization for disk storage
    virtual nlohmann::json serialize() const = 0;

protected:
    // Lossless Roundtripping: Holds any unmapped JSON keys found during API response/history parsing.
    // Kept out of core C++ data structures to save memory, but merged back during serialization.
    nlohmann::json extra_fields_;
};

} // namespace agentlib
```

#### Concrete Subclasses & Formats

##### 1. `system_turn`
* **Purpose:** Holds system instructions (e.g. active coding guidelines, active tool schemas, or temporary context instructions).
* **Data Fields:**
  * `std::string content`
  * `std::string purpose` (e.g. `"base"`, `"skills"`, `"tool_family"`)
* **to_messages:**
  ```json
  [ { "role": "system", "content": "<content>" } ]
  ```
* **to_markdown:**
  ```markdown
  > [!NOTE]
  > **System Instructions (<purpose>):**
  > <content>
  ```

##### 2. `user_turn`
* **Purpose:** Represents the developer's inputs.
* **Data Fields:**
  * `std::string content`
  * `std::optional<std::string> name` (optional developer identifier)
* **to_messages:**
  ```json
  [ { "role": "user", "content": "<content>", "name": "<name>" } ]
  ```
* **to_markdown:**
  ```markdown
  ### User
  <content>
  ```

##### 3. `model_response_turn`
* **Purpose:** Holds LLM text output, internal thoughts (e.g., DeepSeek `<think>` blocks), and any requested tool calls.
* **Data Fields:**
  * `std::string content` (response text)
  * `std::optional<std::string> reasoning_content` (thinking content)
  * `std::vector<tool_call> tool_calls` (calls to execute)
  * `std::string response_id` (chaining tracking identifier)
* **to_messages:**
  * Maps role to `"assistant"`. 
  * *Compaction Level 1:* Strips `reasoning_content` and `<think>` tags from `content`.
  * *Compaction Level 2:* Additionally clears the `content` block if `tool_calls` is non-empty (stripping pseudo-reasoning).
* **to_markdown:**
  ```markdown
  <think>
  <reasoning_content>
  </think>

  <content>

  *Requested Tools:*
  * `tool_name` (ID: `call_id`) with arguments `...`
  ```

##### 4. `tool_execution_turn`
* **Purpose:** Groups results of the tool calls requested in the preceding assistant turn (representing a transactional boundaries block).
* **Data Fields:**
  * `struct tool_result { std::string call_id; std::string name; std::string content; bool is_error; }`
  * `std::vector<tool_result> results`
* **to_messages:** Maps results into individual tool-role messages:
  ```json
  [ { "role": "tool", "tool_call_id": "<call_id>", "content": "<content>" } ]
  ```
* **to_markdown:**
  ```markdown
  #### Tool Executions:
  
  **`tool_name`** (ID: `call_id`) - *Success*
  ```cpp
  <content>
  ```
  ```

##### 5. `error_turn`
* **Purpose:** Captures developer-facing execution errors (safety blocks, command execution timeouts) that should not be transmitted to the LLM but need to be visually logged.
* **Data Fields:**
  * `std::string error_message`
* **to_messages:** Returns an empty vector.
* **to_markdown:**
  ```markdown
  > [!WARNING]
  > **Execution Error:** <error_message>
  ```

### B. Episode (`agentlib::Episode`)
A sequential history of turns representing an archived or active slice of the conversation.
```cpp
namespace agentlib {

class Episode {
public:
    Episode(std::string id, std::string title, std::string summary);

    std::string get_id() const;
    std::string get_title() const;
    std::string get_summary() const;
    
    // Compaction level (0 = raw, 1 = think-free, 2 = think-free + no-pseudo-text, 99 = archived/summary-only)
    int get_compaction_level() const;
    void set_compaction_level(int level);

    // Turn collection
    void add_turn(std::shared_ptr<Turn> turn);
    const std::vector<std::shared_ptr<Turn>>& get_turns() const;

    // Aggregates LLM messages for this episode:
    // - If level == 99 (archived): returns a single system message summary.
    // - Otherwise: loops over turns and delegates to turn->to_messages(caps, compaction_level_).
    std::vector<message> to_messages(const model_capabilities& caps) const;

    // Serialization
    nlohmann::json serialize() const;
    static std::shared_ptr<Episode> deserialize(const nlohmann::json& j);
};

} // namespace agentlib
```

### C. Conversation (`agentlib::Conversation`)
High-level coordinator of active/archived episodes, model settings, and streaming/session updates.
```cpp
namespace agentlib {

class Conversation {
public:
    // Active Episode Management
    std::shared_ptr<Episode> get_current_episode() const;
    const std::vector<std::shared_ptr<Episode>>& get_episodes() const;
    void archive_current_episode(const std::string& title, const std::string& summary);
    void create_new_episode(const std::string& title);

    // Active Model & Settings
    std::shared_ptr<ai_model> get_model() const;
    void set_model(std::shared_ptr<ai_model> model);

    // Re-seeding / World View
    // Returns active editor workspace context (open files, compiler diagnostics) to rebuild system prompt.
    std::string get_current_world_view() const;

    // Appending inputs
    void add_turn(std::shared_ptr<Turn> turn);
    
    // Streaming append helper: Appends token chunks to the latest active turn.
    void append_to_current_turn(const std::string& chunk);
};

} // namespace agentlib
```

---

## Key Flows

### 1. Transport & LLM Invocation
* Transports receive the `Conversation` reference.
* To generate the request body, the transport iterates over `conversation.get_episodes()` and maps each episode into messages using `episode->to_messages(model_capabilities)`.
* For protocol-specific re-seeding (e.g. Gemini system instructions, stateful turn resets, or model switches), the transport queries the conversation's active properties and dynamic world-view metadata.

### 2. Streaming Response Flow
1. When the client receives a token chunk from the network stream, it invokes `conversation->append_to_current_turn(chunk)`.
2. The UI listens to conversation changes (or gets triggered by callbacks) and refreshes the display using the Markdown representations of the active turns.