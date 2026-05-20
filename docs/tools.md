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

## 4. UI Overlays

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

---

## 5. Environment & Misc

### `ask_user`
*   **Description:** Ask the user one or more questions to gather preferences, clarify requirements, or make decisions. When using this tool, prefer providing multiple-choice options. An 'Other' text input field is automatically added.
*   **Arguments:**
    *   `questions` *(array of objects, required)*:
        *   `question` *(string, required)*: The complete question to ask the user.
        *   `options` *(array of strings, optional)*: The selectable choices for the question.

### `activate_skill`
*   **Description:** Activates a specialized agent skill by name. Returns the skill's instructions wrapped in `<activated_skill>` tags. These provide specialized guidance for the current task. Use this when you identify a task that matches a skill's description. ONLY use names exactly as they appear in the *Available Skills* section.
*   **Arguments:**
    *   `name` *(string, required)*: The name of the skill to activate.

### `get_temperature`
*   **Description:** (Demo tool) Get the current temperature in a given location.
*   **Arguments:**
    *   `location` *(string, required)*: The location to check, e.g., "San Francisco, CA" or "Mars".
