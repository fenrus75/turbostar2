# LLM Agent Architecture (Turbostar)

This document describes the core architecture of the LLM Agent subsystem in Turbostar, which is located in `src/agentlib/`.

For information on how to implement specific tools for the agent, see [design-tools.md](file:///home/arjan/git/turbostar2/docs/design-tools.md).

---

## 1. Core Agent Loop
The central coordinator is the `ai_agent` class, which interacts with the `ai_model` and `llm_client`.
* **`ai_agent`**: Manages the high-level conversation loop, executes tools, handles telemetry, and triggers compaction.
* **`llm_client`**: Handles the communication with the LLM backend (e.g., Gemini API, OpenAI), formatting payloads and parsing raw streaming or static responses.

---

## 2. Pluggable Transport Layer
The agent uses a pluggable transport layer implementing `llm_transport` to handle network communication and facilitate deterministic End-to-End testing.

* **`httplib_transport`**: The live HTTP client used in normal operation to connect to model servers.
* **`recording_transport`**: Captures live HTTP traffic to disk during test creation.
* **`replay_transport`**: Mocks the LLM by replaying recorded traffic. This allows E2E tests to run quickly and deterministically without requiring a live internet connection or API keys.

---

## 3. Class Hierarchy & Data Model

The data model represents the conversation history as a structured tree, completely decoupled from rendering details and NCurses/TUI dependencies:

```
Conversation (coordinator of active model, settings, and session status)
  └── Episode (logical slice of history, e.g. archived or active page-out blocks)
        └── Transaction (a single request-response-execution exchange / visual Turn Box)
              └── Turn (atomic messages/events / visual Sub-panels)
```

### Core Separation of Concerns:
* **Data Layer:** Core classes (`Turn`, `Transaction`, `Episode`, `Conversation`) store text content and metadata. They have no knowledge of TUI cells or NCurses frames.
* **UI Layer:** Visual presentation is driven by standard **Markdown representations** exported by transactions and turns (`to_markdown()`), or dynamically rendered using a bound `agent_interaction` View-Model.
* **Active Tool Ownership & Invalidation:**
  * Tool registries, active tool families, and execution states are owned and managed exclusively by `ai_agent`.
* **Sequence Numbers and Dynamic Querying:**
  * Every `Turn` has a monotonic sequence number allocated from the `Conversation` (serialized as `sequence_number`).
  * `Transaction` and `Episode` maintain cached `min_turn` and `max_turn` sequence numbers which are serialized to JSON.
  * `Conversation::get_turns_since(seq)` provides a fast query vector of all new turns since a specific sequence number, skipping entire episodes or transactions if their `max_turn` is less than or equal to the query index.
  * `Conversation` is a pure data container. When the agent changes active tools/families or modifies history, it calls `conversation->invalidate_history(true)`. The transport layer checks this flag before making requests to determine if it must reset session/thread contexts.
* **UI Mapping:**
  * A `Transaction` maps 1:1 to a single bordered TUI **Turn Box**.
  * A `Turn` maps 1:1 to a TUI **Sub-panel** inside that box (separated by horizontal lines, e.g., `─── Thinking ───`).

---

## 4. Class Interfaces

### A. Turn (`agentlib::Turn`)
A leaf event in the conversation.

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

    // Generates raw LLM messages for this specific turn.
    // Respects the active compaction level of the parent episode.
    virtual std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const = 0;

    // UI View-Model Output: Generates a clean Markdown representation of the turn contents.
    virtual std::string to_markdown() const = 0;

    // View-Model binding
    std::shared_ptr<agent_interaction> get_interaction() const { return interaction_; }
    void set_interaction(std::shared_ptr<agent_interaction> view) { interaction_ = view; }

    // Streaming updates: appends incremental token chunks (for text or reasoning streams).
    // Updates end_time of the time_range on each append.
    virtual void append_content(const std::string& chunk) {
        content_ += chunk;
        if (interaction_) {
            interaction_->push_incremental_content(chunk);
        }
    }

    // Telemetry and sizing
    virtual int estimate_token_count(int compaction_level) const = 0;
    virtual time_range get_time_range() const = 0;

    // Serialization for disk storage
    virtual nlohmann::json serialize() const = 0;

protected:
    std::string content_;
    std::shared_ptr<agent_interaction> interaction_;
    time_range range_;

    // Lossless Roundtripping: Holds any unmapped JSON keys found during API response/history parsing.
    // Kept out of core C++ data structures to save memory, but merged back during serialization.
    nlohmann::json extra_fields_;
};

} // namespace agentlib
```

#### Concrete Turn Subclasses

##### 1. `system_turn`
* **Purpose:** Holds system instructions (active rules, schemas, or context overrides).
* **Data Fields:** `std::string content`, `std::string purpose` (e.g. `"base"`, `"skills"`, `"tool_family"`).
* **estimate_token_count:** Returns exact or char-based approximation (`content.size() / 4`). Unchanged across compaction levels.
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
* **Purpose:** Represents input from the developer.
* **Data Fields:** `std::string content`, `std::optional<std::string> name` (optional developer name).
* **estimate_token_count:** Returns char-based approximation (`content.size() / 4`). Unchanged across compaction levels.
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
* **estimate_token_count:**
  * *Level 0 (Current):* Calculates tokens for both `content` and `reasoning_content`.
  * *Level 1:* Strips `reasoning_content` (approximate tokens subtraction).
  * *Level 2:* Strips both `reasoning_content` and `content` (returns 0 or minimal tool metadata overhead) if `tool_calls` is non-empty.
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
* **Purpose:** Groups execution results for the tool calls requested in the preceding assistant turn.
* **Data Fields:**
  * `struct tool_result { std::string call_id; std::string name; std::string content; bool is_error; }`
  * `std::vector<tool_result> results`
* **estimate_token_count:** Sum of tool results content. Specialized tool subclasses can override this to reflect compaction (e.g. compiler turn output filtering).
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
* **Data Fields:** `std::string error_message`.
* **estimate_token_count:** Returns 0 (not sent to LLM).
* **to_messages:** Returns an empty vector.
* **to_markdown:**
  ```markdown
  > [!WARNING]
  > **Execution Error:** <error_message>
  ```

---

### B. Transaction (`agentlib::Transaction`)
A grouped, atomic exchange (e.g., User Prompt + LLM Output + Tool Results).

```cpp
namespace agentlib {

enum class transaction_type {
    user_exchange,      // User prompt + Assistant response + Tool execution
    system_injection,   // Environment updates or security events
    subagent_lifecycle  // Subagent spawn or final report events
};

class Transaction {
public:
    Transaction(std::string id, transaction_type type);

    std::string get_id() const;
    transaction_type get_type() const;

    // Aggregates LLM messages from all turns in this transaction.
    std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const;

    // UI View-Model: Generates a unified Markdown string containing all turns,
    // formatted to fit inside a single visual TUI Turn Box.
    std::string to_markdown() const;

    // Turn Management
    void add_turn(std::shared_ptr<Turn> turn);
    const std::vector<std::shared_ptr<Turn>>& get_turns() const;

    // Telemetry and sizing: Sum of children estimated tokens
    int estimate_token_count(int compaction_level) const;

    // Chronological span: min/max of children range bounds
    time_range get_time_range() const;

    // Serialization
    nlohmann::json serialize() const;
    static std::shared_ptr<Transaction> deserialize(const nlohmann::json& j);
};

} // namespace agentlib
```

---

### C. Episode (`agentlib::Episode`)
A sequential history of transactions representing an archived or active slice of the conversation.

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

    // Transaction collection
    void add_transaction(std::shared_ptr<Transaction> transaction);
    const std::vector<std::shared_ptr<Transaction>>& get_transactions() const;

    // Sizing telemetry:
    // - If level == 99 (archived): returns size of the archived summary string.
    // - Otherwise: sum of child transaction tokens at compaction_level_ state.
    int estimate_token_count(int compaction_level) const;

    // Chronological span: min/max of transaction range bounds
    time_range get_time_range() const;

    // Aggregates LLM messages for this episode:
    // - If level == 99 (archived): returns a single system message summary.
    // - Otherwise: loops over transactions and calls transaction->to_messages(caps, compaction_level_).
    std::vector<message> to_messages(const model_capabilities& caps) const;

    // Serialization
    nlohmann::json serialize() const;
    static std::shared_ptr<Episode> deserialize(const nlohmann::json& j);
};

} // namespace agentlib
```

---

### D. Conversation (`agentlib::Conversation`)
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

    // Invalidation tracking (e.g. set on history changes, compaction, model switches, tool updates)
    // Transports query this to reset stateful session/thread contexts.
    bool is_history_invalidated() const;
    void invalidate_history(bool invalid);

    // Sizing telemetry: Sum of episode tokens based on their active compaction level settings
    int estimate_token_count() const;

    // Chronological span: min/max of active episode range bounds
    time_range get_time_range() const;

    // Appending inputs
    void add_transaction(std::shared_ptr<Transaction> transaction);
    
    // Streaming append helper: Resolves active leaf turn and appends token chunk.
    void append_to_current_turn(const std::string& chunk);
};

} // namespace agentlib
```

---

## 5. Telemetry & Time Tracking
To track execution latency (e.g., inference time, compilation durations) and calculate chronological spans, every layer in the hierarchy supports the `time_range` interface:

```cpp
namespace agentlib {

struct time_range {
    uint64_t start_time{0}; // Unix timestamp when event/processing started
    uint64_t end_time{0};   // Unix timestamp when event/processing finished or last updated
};

} // namespace agentlib
```

### Time Accumulation Rules:
* **Turn Layer:** `start_time` is set when the first token arrives (or gets sent). `end_time` begins equal to `start_time` and is updated on each streaming append or log flush. The difference (`end_time - start_time`) represents the exact duration of compilation, inference, or user editing.
* **Container Layers (Transaction, Episode, Conversation):** `start_time` is recursively calculated as `min(child.start_time)` and `end_time` is recursively calculated as `max(child.end_time)`.

---

## 6. Interactions Subsystem (View-Model Layer)
The `interactions` subsystem (located in `src/agentlib/interactions/`) acts as the ViewModel bridging the gap between raw C++ data models and the Turbo Pascal-style TUI screen.

### Core Architecture
* **`interaction_type`**: A high-level category (e.g., `user_message`, `reasoning`, `terminal`) used by the UI to group related interactions into a single visual "Turn."
* **`interaction_role`**: Defines the semantic purpose of a message (e.g., `agent`, `user`, `thinking`, `system`). This is used by the theme system to resolve colors independently of the interaction's implementation.
* **`agent_interaction` (Base Class)**: Defines the common interface. It handles text wrapping and maintains a rendering cache. It requires implementations of `get_type()` and `get_role()`.
* **Subclasses**:
  * `user_message`: Represents user input.
  * `llm_response`: The textual output from the LLM.
  * `reasoning`: The agent's internal thought process.
  * `tool_interaction`: Visualizes a tool call and its result.
  * `terminal`: Displays raw output from external processes (e.g., Python execution).
  * `action`: A specialized class for high-level operations (like `fs_read_lines`).
  * `system_message`: Context and instructions.

### Unidirectional MVVM Binding
* The `Turn` object optionally owns a reference to an `agent_interaction` View-Model.
* The `agent_interaction` has no back-reference to its `Turn`. Content changes (streaming tokens, incremental execution stdout) are unilaterally pushed from the `Turn` to the `agent_interaction`:

```cpp
namespace agentlib {

class agent_interaction {
public:
    virtual ~agent_interaction() = default;

    // Sets full content statically (e.g. on loading historical turns)
    virtual void push_content(const std::string& full_content) {
        content_ = full_content;
        invalidate_cache();
    }

    // Appends content incrementally (streaming tokens/stdout)
    virtual void push_incremental_content(const std::string& chunk) {
        content_ += chunk;
        invalidate_cache();
    }
};

} // namespace agentlib
```

---

## 7. Themed Grouped Rendering
To maintain visual clarity and a cohesive TUI aesthetic, the `agent_window` does not simply render a flat list of interactions. Instead, it performs a **Grouping Pass**:

### Turn Containers (Turn Boxes)
Multiple related interactions (e.g., a User prompt followed by a Thinking block and a Tool call) are merged into a single **Turn Box**.
* Each box is enclosed in a solid UTF-8 frame (`┌─┐`).
* This eliminates "ragged" background edges and provides clear boundaries between conversation turns.
* **Alternating Backgrounds**: Turn boxes alternate between a **Primary (White)** and **Alternate (Cyan)** background, creating a "ledger" look that aids scannability.

### Sub-Panels
Within a Turn Box, individual interactions are separated by horizontal lines. For complex items like Reasoning or Terminal output, the separator includes a text label (e.g., `─── Thinking ───`) to create a "sub-panel" effect.

### Color Resolution (`agent_theme.h`)
Colors are resolved dynamically using `get_color_pair(role, background_mode)`. This ensures that a "User" message always uses the correct blue-ish foreground, whether it is on a white or cyan background.
* **Exceptions**: Certain high-density outputs keep their distinct backgrounds:
  * **Terminal**: Always "White on Black."
  * **Diffs**: Always "Syntax-themed Blue."

---

## 8. Tool-Turn Hybrid & Plugin Registry

### 1. The `llm_tool` Interface
To support tool-specific compaction or data schemas (e.g., filtering raw compilation logs to essential lines), the tool class handles instantiation of both its data representation (`Turn`) and optional custom visual view-model (`agent_interaction`).

```cpp
class llm_tool {
public:
    virtual ~llm_tool() = default;

    // Factory method: returns the data Turn object for this tool call.
    // Overriding this allows specialized tools to return custom Turn subclasses (e.g., compiler_turn).
    virtual std::shared_ptr<Turn> create_turn(const tool_call& call) const {
        return std::make_shared<tool_execution_turn>(call.id, call.function.name);
    }

    // Creates the visual view-model element (like a custom progress bar or split diff pane).
    // Returns nullptr if the tool uses generic markdown rendering.
    virtual std::shared_ptr<agent_interaction> get_interaction() const {
        return nullptr; 
    }
};
```

### 2. The `TurnRegistry` (Fallback Deserialization)
To support plugin tools loaded dynamically via `.so` files, the engine registers custom turn deserializers. If a plugin is uninstalled, deserialization automatically falls back to the generic `tool_execution_turn` class, avoiding crashes.

```cpp
namespace agentlib {

class TurnRegistry {
public:
    static TurnRegistry& get_instance();

    // Plugins register custom Turn deserializers here on startup
    void register_deserializer(const std::string& turn_type_name, 
                               std::function<std::shared_ptr<Turn>(const nlohmann::json&)> deserializer);

    // Deserializes a turn from JSON, with safety fallback routines
    std::shared_ptr<Turn> deserialize(const nlohmann::json& j) const {
        std::string type = j.value("turn_type", "generic");
        
        if (deserializers_.contains(type)) {
            return deserializers_.at(type)(j);
        }
        
        // Safety Fallback: If plugin is missing, read it as a generic tool execution turn
        if (is_tool_specific_type(type)) {
            return tool_execution_turn::deserialize(j);
        }
        
        return deserialize_core_type(type, j);
    }

private:
    std::map<std::string, std::function<std::shared_ptr<Turn>(const nlohmann::json&)>> deserializers_;
};

} // namespace agentlib
```

---

## 9. Key Flows

### 1. Transport & LLM Invocation
1. Transports receive the `Conversation` reference.
2. To generate the request body, the transport iterates over `conversation.get_episodes()` and maps each episode into messages using `episode->to_messages(model_capabilities)`.
3. For protocol-specific re-seeding (e.g., Gemini system instructions, stateful turn resets, or model switches), the transport queries the conversation's active properties and dynamic world-view metadata.

### 2. Streaming Response Flow
1. When the client receives a token chunk from the network stream, it invokes `conversation->append_to_current_turn(chunk)`.
2. The UI listens to conversation changes (or gets triggered by callbacks) and refreshes the display using the Markdown representations of the active transactions.

---

## 10. Security
The agent enforces strict runtime security. All tools must undergo validation before execution. For full details on the two-stage security model, refer to [design-tools.md](file:///home/arjan/git/turbostar2/docs/design-tools.md).