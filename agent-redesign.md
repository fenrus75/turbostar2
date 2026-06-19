# Agentlib Redesign Blueprint

The current architectural design is documented in `docs/design-agent.md`.

## Goals
Refactor `src/agentlib/` to introduce a clean, modular class hierarchy. This makes it easy to support multiple API backends/protocols (e.g. stateful OpenAI, streaming Gemini, Copilot), simplifies UI rendering separation, and guarantees transactional safety during history compaction and paging.

## Class Hierarchy

```
Conversation (coordinator of active model, settings, and session status)
  └── Episode (logical slice of history, e.g. archived or active page-out blocks)
        └── Transaction (a single request-response-execution exchange / visual Turn Box)
              └── Turn (atomic messages/events / visual Sub-panels)
```

---

## Core Architecture & Separation of Concerns

* **Data Layer:** Core classes (`Turn`, `Transaction`, `Episode`, `Conversation`) are completely decoupled from visual rendering details and NCurses/TUI dependencies.
* **UI Layer:** Visual presentation is driven by standard **Markdown representations** exported by transactions and turns, or dynamically rendered using a bound `agent_interaction` View-Model.
* **Unidirectional MVVM Binding:** 
  * The `Turn` object optionally owns a reference to an `agent_interaction` View-Model.
  * The `agent_interaction` has no back-reference to its `Turn`. Content changes are unilaterally pushed from the `Turn` to the `agent_interaction`.
* **UI Mapping:** 
  * A `Transaction` maps 1:1 to a single bordered TUI **Turn Box**.
  * A `Turn` maps 1:1 to a TUI **Sub-panel** inside that box (separated by horizontal lines, e.g. `─── Thinking ───`).

---

## Telemetry & Time Tracking

To track execution latency (e.g. inference time, compilation durations) and calculate chronological spans, every layer in the hierarchy supports the `time_range` interface.

```cpp
namespace agentlib {

struct time_range {
    uint64_t start_time{0}; // Unix timestamp (seconds or milliseconds) when event/processing started
    uint64_t end_time{0};   // Unix timestamp when event/processing finished or last updated
};

} // namespace agentlib
```

### Time Accumulation Rules:
* **Turn Layer:** 
  * `start_time` is set when the first token arrives (or gets sent).
  * `end_time` begins equal to `start_time` (e.g. for one-shot events) and is updated on each streaming append or log flush. The difference (`end_time - start_time`) represents the exact duration of compilation, inference, or user editing.
* **Container Layers (Transaction, Episode, Conversation):**
  * `start_time` is recursively calculated as `min(child.start_time)`.
  * `end_time` is recursively calculated as `max(child.end_time)`.

---

## Class Interfaces

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
* **Data Fields:**
  * `std::string content`
  * `std::string purpose` (e.g. `"base"`, `"skills"`, `"tool_family"`)
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
* **Data Fields:**
  * `std::string content`
  * `std::optional<std::string> name` (optional developer name)
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
* **Data Fields:**
  * `std::string error_message`
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
    int estimate_token_count(int compaction_level) const {
        int sum = 0;
        for (const auto& turn : turns_) {
            sum += turn->estimate_token_count(compaction_level);
        }
        return sum;
    }

    // Chronological span: min/max of children range bounds
    time_range get_time_range() const {
        if (turns_.empty()) return {};
        time_range consolidated{ UINT64_MAX, 0 };
        for (const auto& turn : turns_) {
            time_range r = turn->get_time_range();
            consolidated.start_time = std::min(consolidated.start_time, r.start_time);
            consolidated.end_time = std::max(consolidated.end_time, r.end_time);
        }
        return consolidated;
    }

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
    int estimate_token_count(int compaction_level) const {
        if (compaction_level_ == 99) {
            // Estimate size of "[SYSTEM MEMORY: Archived Episode]..." summary instruction
            return static_cast<int>((title_.size() + summary_.size()) / 4 + 50);
        }
        int sum = 0;
        for (const auto& tx : transactions_) {
            sum += tx->estimate_token_count(compaction_level);
        }
        return sum;
    }

    // Chronological span: min/max of transaction range bounds
    time_range get_time_range() const {
        if (transactions_.empty()) return {};
        time_range consolidated{ UINT64_MAX, 0 };
        for (const auto& tx : transactions_) {
            time_range r = tx->get_time_range();
            consolidated.start_time = std::min(consolidated.start_time, r.start_time);
            consolidated.end_time = std::max(consolidated.end_time, r.end_time);
        }
        return consolidated;
    }

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

    // Sizing telemetry: Sum of episode tokens based on their active compaction level settings
    int estimate_token_count() const {
        int sum = 0;
        for (const auto& ep : episodes_) {
            sum += ep->estimate_token_count(ep->get_compaction_level());
        }
        return sum;
    }

    // Chronological span: min/max of active episode range bounds
    time_range get_time_range() const {
        if (episodes_.empty()) return {};
        time_range consolidated{ UINT64_MAX, 0 };
        for (const auto& ep : episodes_) {
            time_range r = ep->get_time_range();
            consolidated.start_time = std::min(consolidated.start_time, r.start_time);
            consolidated.end_time = std::max(consolidated.end_time, r.end_time);
        }
        return consolidated;
    }

    // Appending inputs
    void add_transaction(std::shared_ptr<Transaction> transaction);
    
    // Streaming append helper: Resolves active leaf turn and appends token chunk.
    void append_to_current_turn(const std::string& chunk);
};

} // namespace agentlib
```

---

### E. Tool-Turn Hybrid & Plugin Registry

#### 1. The `llm_tool` Interface
To support tool-specific compaction or data schemas (e.g. filtering raw compilation logs to essential lines), the tool class handles instantiation of both its data representation (`Turn`) and optional custom visual view-model (`agent_interaction`).

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

#### 2. The `TurnRegistry` (Fallback Deserialization)
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

### F. View-Model Content Push APIs (`agentlib::agent_interaction`)

The View-Model receives content pushed unilaterally from its owning `Turn` (e.g. streaming LLM reasoning or tool logs).

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
2. The UI listens to conversation changes (or gets triggered by callbacks) and refreshes the display using the Markdown representations of the active transactions.