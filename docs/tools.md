# Turbostar LLM Agent Tools

This document outlines all the built-in LLM tools currently registered in the Turbostar `tool_registry`. These tools are provided to the LLM backend via the standard OpenAI function-calling schema, allowing the agent to perform actions securely within the Turbostar workspace.

All tools are validated through a robust two-stage pipeline. Path resolution automatically prevents directory traversal (e.g., `../../`) and ensures operations respect the permissions enforced by the `file_security_manager`.

---

## 1. File System Reading & Inspection

### `fs_list_dir`
*   **Description:** Lists the contents of a directory as a Markdown table (Type, Size, Lines, Permissions).
*   **Arguments:**
    *   `path` *(string, required)*: The path to the directory, relative to the project root.

### `fs_read_lines`
*   **Description:** Reads a specific range of text lines from a file. Note: The maximum number of lines that can be read in a single call is strictly limited to 2000.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.
    *   `start_line` *(integer, optional)*: The 1-based line number to start reading from. Defaults to 1 if omitted.
    *   `end_line` *(integer, optional)*: The 1-based line number to end reading at (inclusive). Defaults to start_line + 1999 if omitted. A maximum of 2000 lines will be returned.

### `fs_read_binary`
*   **Description:** Reads binary content from a file and returns it as a base64 encoded string. Can read a specific range using start_offset and size.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.
    *   `start_offset` *(integer, optional)*: The 0-based byte offset to start reading from. Defaults to 0.
    *   `size` *(integer, optional)*: The number of bytes to read. Defaults to reading the rest of the file if omitted. A maximum limit (e.g., 50MB) may apply.

### `fs_regexp_lines`
*   **Description:** Search for a regular expression within a file and return matching lines as a Markdown table.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.
    *   `pattern` *(string, required)*: The C++ `std::regex` pattern to search for (e.g., `'function.*foo'`).

### `fs_file_size`
*   **Description:** Get the size of a file in bytes.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file.

---

## 2. File System Mutation

*Note: All mutation tools enforce write-access permissions and strictly prevent modifying files currently active in an editor buffer to avoid race conditions.*

### `fs_replace_lines`
*   **Description:** Surgically edit a file by providing an array of line operations (add, remove, replace). Edits MUST be sorted in descending `line_number` order to prevent line-shifting offsets.
*   **Arguments:**
    *   `path` *(string, required)*: Path to the file to edit, relative to the project root.
    *   `edits` *(array of objects, required)*: A list of edit operations.
        *   `line_number` *(integer, required)*: The 1-based line number to target.
        *   `type` *(string, required)*: The type of edit operation (`add`, `remove`, `replace`).
        *   `original_text` *(string, optional)*: Required for `remove` and `replace`. The exact full content of the original line being modified. Used for safety verification. Pass empty string for `add`.
        *   `replace_with` *(string, optional)*: Required for `add` and `replace`. The exact new content to insert or replace the line with. Pass empty string for `remove`.

### `fs_write_file`
*   **Description:** Creates a new file or completely overwrites an existing file with the provided content.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file to write, relative to the project root.
    *   `content` *(string, required)*: The entire complete content to write into the file.
    *   `force_overwrite` *(boolean, optional)*: Set to true to overwrite an existing file. Defaults to false.

---

## 3. Compilation & Diagnostics

### `fs_run_tests`
*   **Description:** Runs the project's test suite (synchronously) and returns the console output. Catch crashes and dumps backtraces. Runs with terminal interaction.
*   **Arguments:** None.

### `fs_compile_project`
*   **Description:** Compiles the entire project (synchronously) and returns the raw console output. Populates the workspace error list.
*   **Arguments:** None.

### `fs_compile_file`
*   **Description:** Compiles a single file (synchronously) and returns the raw console output. Populates the workspace error list.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file to compile, relative to the project root.

### `fs_compile_summary`
*   **Description:** Reports all files that currently have compilation errors/warnings or live LSP diagnostics. Returns a Markdown table.
*   **Arguments:** None.

### `fs_compile_info`
*   **Description:** Retrieves the exact compile command (from `compile_commands.json`), the last compile time, and any active build/LSP diagnostics for a specific file.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.

---

## 4. UI Overlays & Feedback

### `flag_as_error`
*   **Description:** Flags a specific line (and optional column range) in a file as an error or warning, creating a diagnostic overlay in the editor UI (red/yellow backgrounds).
*   **Arguments:**
    *   `filename` *(string, required)*: The relative path to the file.
    *   `line` *(integer, required)*: The 1-based line number of the error.
    *   `column` *(integer, required)*: The 1-based start column number of the error. Use 1 if unknown.
    *   `length` *(integer, required)*: The length of the error highlight in characters. Use 0 to highlight the whole line.
    *   `error_string` *(string, required)*: The description of the error.
    *   `is_warning` *(boolean, required)*: True if this is a warning, false if it is a hard error.

### `clear_all_errors`
*   **Description:** Clears all currently flagged errors and warnings from the editor UI.
*   **Arguments:** None.

### `agent_set_status`
*   **Description:** Sets a brief status message in the editor's status bar to inform the user of progress.
*   **Arguments:**
    *   `message` *(string, required)*: The brief status message (e.g., 'Analyzing code...').

---

## 5. Semantic Code Analysis (LSP)

These tools provide semantic understanding of code by leveraging the Language Server Protocol (LSP). They are available for supported languages (currently C++ and Python).

### `code_get_scope`
*   **Description:** Returns the semantic hierarchy of code blocks (function, class, etc.) containing a specific location.
*   **Arguments:**
    *   `path` *(string, required)*: The file path.
    *   `line` *(integer, required)*: The 1-based line number.
    *   `character` *(integer, required)*: The 0-based character offset.

### `code_get_definition`
*   **Description:** Finds the definition(s) of a symbol at a specific location, potentially across multiple files.
*   **Arguments:**
    *   `path` *(string, required)*: The file path.
    *   `line` *(integer, required)*: The 1-based line number.
    *   `character` *(integer, required)*: The 0-based character offset.

### `code_get_references`
*   **Description:** Finds all references (usages) of a symbol across the project.
*   **Arguments:**
    *   `path` *(string, required)*: The file path.
    *   `line` *(integer, required)*: The 1-based line number.
    *   `character` *(integer, required)*: The 0-based character offset.

---

## 6. Environment & Misc

### `ask_user`
*   **Description:** Ask the user one or more questions to gather preferences, clarify requirements, or make decisions. When using this tool, prefer providing multiple-choice options. An 'Other' text input field is automatically added.
*   **Arguments:**
    *   `questions` *(array of objects, required)*:
        *   `question` *(string, required)*: The complete question to ask the user.
        *   `options` *(array of strings, optional)*: The selectable choices for the question.

### `activate_skill`
*   **Description:** Activates a specialized agent skill by name. Returns the skill's instructions wrapped in `<skill_content>` tags. These provide specialized guidance for the current task. Use this when you identify a task that matches a skill's description. ONLY use names exactly as they appear in the *Available Skills* section.
*   **Arguments:**
    *   `name` *(string, required)*: The name of the skill to activate.

### `list_skills`
*   **Description:** Lists all available specialized agent skills. Returns a Markdown table containing the skill name, URI, and description. Use this to discover available skills.
*   **Arguments:** None.

### `get_current_datetime`
*   **Description:** Returns the current date and time as a markdown table. Includes Unix time, Year, Month, Day, Hour, Minute, Second, and Timezone.
*   **Arguments:** None.

### `list_tool_calls`
*   **Description:** Lists all available LLM tools and their descriptions as a Markdown table. Use this to introspect your capabilities.
*   **Arguments:** None.

### `run_python`
*   **Description:** Executes Python code in a sandboxed environment.
*   **Arguments:**
    *   `code` *(string, optional)*: The raw Python code string to execute.
    *   `file_path` *(string, optional)*: The relative path to a Python script to execute.
    *   `dependencies` *(array of strings, optional)*: PyPI dependencies to temporarily install via 'uv' (if available).

---

## 7. Agent State & To-Do Management

### `agent_add_todo`
*   **Description:** Adds a new task to the AI agent's internal todo list. Use this to track steps during complex multi-part requests.
*   **Arguments:**
    *   `text` *(string, required)*: The description of the task to add.

### `agent_list_todos`
*   **Description:** Lists all tasks currently in the AI agent's internal todo list, formatted as markdown checkboxes.
*   **Arguments:** None.

### `agent_complete_todo`
*   **Description:** Marks a task as complete in the AI agent's internal todo list.
*   **Arguments:**
    *   `text` *(string, required)*: The exact task text or a unique substring to match.

### `agent_delete_todo`
*   **Description:** Deletes a task from the AI agent's internal todo list.
*   **Arguments:**
    *   `text` *(string, required)*: The exact task text or a unique substring to match.

---

## 8. Subagent Orchestration

### `create_agent`
*   **Description:** Creates a new subagent to delegate tasks to.
*   **Arguments:**
    *   `name` *(string, required)*: A short, descriptive name for the subagent.
    *   `profile` *(string, required)*: The initial prompt and system instructions for the subagent.

### `list_agents`
*   **Description:** Lists all active subagents managed by the current agent. Returns a Markdown table of ID, Name, and Status.
*   **Arguments:** None.

### `agent_status`
*   **Description:** Returns detailed status information about a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to query.

### `message_agent`
*   **Description:** Sends a message or command to an active subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent.
    *   `message` *(string, required)*: The text message or instruction to send.

### `wait_for_agent`
*   **Description:** Pauses execution until the specified subagent becomes idle.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to wait for.

### `agent_get_output`
*   **Description:** Retrieves the entire interaction history of a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to query.

### `end_agent`
*   **Description:** Closes and terminates a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to terminate.

### `agent_todo_status`
*   **Description:** Returns the todo list with completion status of a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to query.

---

## 9. Crashdump & Crash Analysis

### `crashdump_list`
*   **Description:** Returns a markdown table of recent crashdumps (crashes) generated by commands in the sandbox.
*   **Arguments:** None.

### `crashdump_get_info`
*   **Description:** Retrieves the detailed backtrace and GDB analysis of a specific crashdump.
*   **Arguments:**
    *   `pid` *(integer, required)*: The Process ID (PID) of the crashed executable.

---

## 10. SQLite Database Operations

### `sqlite_create_db`
*   **Description:** Creates a new persistent SQLite database for the project.
*   **Arguments:**
    *   `database` *(string, required)*: The simple name of the database to create (no paths or extensions).

### `sqlite_delete_db`
*   **Description:** Deletes an existing SQLite database for the project.
*   **Arguments:**
    *   `database` *(string, required)*: The simple name of the database to delete.

### `sqlite_list_db`
*   **Description:** Lists all persistent SQLite databases available for the project.
*   **Arguments:** None.

### `sqlite_perform`
*   **Description:** Executes arbitrary SQL queries on a persistent SQLite database. Returns results as a Markdown table.
*   **Arguments:**
    *   `database` *(string, required)*: The simple name of the database to query.
    *   `query` *(string, required)*: The SQL command(s) to execute.
