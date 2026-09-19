# Command Registry (`command_registry`)

## Goals
The `command_registry` class manages the lifecycle, registration, lookup, and self-documentation of slash commands in Turbostar's agent interactive chat window. Slash commands allow the user to execute direct control directives (such as switching models, configuring MCP servers, triggering context compaction, saving conversations, or toggling execution modes like YOLO) without confusing the LLM with meta-instructions.

## Architecture and Constraints
- **Singleton Pattern**: Exposed via `command_registry::get_instance()` with strict thread-safe registration and access guarded by `std::mutex mutex_`.
- **Decoupled Command Execution**: Commands implement the abstract `agent_command` interface (`get_name()`, `get_description()`, `execute()`), taking a unified `agent_command::context` holding pointers to the active `ai_agent`, `window_id`, arguments string, and event queue.
- **Dynamic Extensibility**: Built-in commands are registered on initialization, and external plugins can dynamically register/unregister slash commands (e.g. `grill-me`).
- **Self-Documentation**: The built-in `/help` command reflects the current list of registered commands dynamically by querying `get_command_names()`.

## Lessons Learned
- **Prompt vs Command Distinction**: Keeping slash commands interceptable before sending text to the LLM prevents prompt pollution and unintended token generation.
- **Thread Safety for Dynamic State**: When slash commands modify global state like `yolo_mode`, ensure underlying fields in `config_manager` use atomic primitives (`std::atomic<bool>`) so concurrent worker threads and tool execution loops observe changes immediately without data races.
- **Argument Flexibility**: Commands should accept case-insensitive boolean/toggle arguments (e.g. `/yolo on`, `/yolo off`, `/yolo 1`, `/yolo 0`, or toggling if empty) and sanitize whitespace to provide an intuitive UX.
