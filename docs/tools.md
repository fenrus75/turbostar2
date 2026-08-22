# Turbostar LLM Agent Tools

This document outlines all the built-in LLM tools currently registered in the Turbostar `tool_registry`. These tools are provided to the LLM backend via the standard OpenAI function-calling schema, allowing the agent to perform actions securely within the Turbostar workspace.

All tools are validated through a robust two-stage pipeline. Path resolution automatically prevents directory traversal (e.g., `../../`) and ensures operations respect the permissions enforced by the `file_security_manager`.

---

## Tool Parameter Naming & Description Guidelines

All tools in Turbostar follow a **"Standardized Unless..."** design policy for parameter names and parameter descriptions:

### Parameter Naming Rules
1. **Primary Input Target**: Always use **`path`** for single file or directory input targets. Do NOT use `filename`, `file_path`, or `filepath` for general filesystem paths.
2. **Input + Output Operations**: For format conversions or transformations taking an input target and producing an output file, use **`path`** (or `input_path`) for input, and **`output_path`** for destination output. Both support workspace relative paths and VFS URIs (`tmp://...`).
3. **Item & Result Count Caps**: Always use **`limit`** when capping the maximum number of items, search results, files, or entries returned by a list or query tool. Do NOT use `max_results` or `count`.
4. **Byte Range Slicing**: Always use **`offset`** for start byte position and **`size`** for number of bytes to read or process. Do NOT use `length` for byte range sizes.
5. **Domain-Specific Name Spaces**: Tools operating strictly in non-filesystem name spaces (such as `images://` asset names in image tools or database names in database tools) may use domain-appropriate parameter names like `filename` or `database`.

### Parameter Description Rules
1. **Standard Base Description**: Every `path` and `output_path` parameter description MUST start with the standard base phrase:
   - **For file paths:** `"Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt')."`
   - **For directory paths:** `"Relative directory path under the project workspace or VFS URI (e.g., 'tmp://dir')."`
2. **Tool-Specific Additions**: If a tool has specific requirements or rules for its path argument, append it as a **second sentence** directly following the standard base description.

### Tool Purity & Read-Only Execution
Tools in Turbostar implement `is_pure()` to enforce read-only agent security rules. A tool call is **PURE** (`is_pure() == true`) if it does not modify, delete, or overwrite persistent files in the Project Codebase or Workspace Source Tree. Read-only agents are permitted to execute pure tools, agent workflow tools (`activate_tool_family`, `invoke_subagent`, `report_final_result`), code review database tools (`create_code_review_item`), in-memory VFS memory (`images://`), and temporary scratchpads (`tmp://`). See [design-pure.md](file:///home/arjan/git/turbostar2/docs/design-pure.md) for full specification.

---

## 1. File System Reading & Inspection

### `fs_list_dir`
*   **Description:** Lists the contents of a directory as a Markdown table (Type, Size, Lines, Permissions, and optionally rich metadata). ALWAYS use this tool to list directory contents instead of running `ls` in a shell command.
*   **Arguments:**
    *   `path` *(string, required)*: Relative directory path under the project workspace or VFS URI (e.g., 'tmp://dir').
    *   `rich_metadata` *(boolean, optional)*: If true, runs file header inspection to detect MIME types and format metadata (e.g. image dimensions, ELF architectures).
    *   `limit` *(integer, optional)*: Maximum number of files to return in the list. Defaults to 100.
    *   `offset` *(integer, optional)*: Starting offset for pagination. Defaults to 0.

### `fs_read_lines`
*   **Description:** Reads a specific range of text lines from a file. Output lines are prefixed with their 1-based line number in `"<line_number>: <line_text>"` format. Automatically appends a compact symbol codemap overview table when reading a partial range of a source or header file.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').
    *   `start_line` *(integer, optional)*: The 1-based line number to start reading from. Defaults to 1 if omitted. Mutually exclusive with `tail`.
    *   `end_line` *(integer, optional)*: The 1-based line number to end reading at (inclusive). Defaults to reading the rest of the file if omitted. Mutually exclusive with `tail`.
    *   `tail` *(integer, optional)*: Reads the specified number of lines from the end of the file. Mutually exclusive with `start_line` and `end_line`.

### `fs_read_symbol`
*   **Description:** Read the full definition of a function, method, class, struct, or variable by name from a file. Uses the LSP server to locate the exact symbol boundaries and returns the code chunk with line numbers.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').
    *   `symbol_name` *(string, required)*: The name of the function, method, class, struct, or variable to read. Supports namespace/class scopes (e.g. `Class::method`).

### `fs_file_codemap`
*   **Description:** Provide a symbol codemap overview table for a file showing functions, methods, classes, and structs along with their start line, end line numbers, and total lines.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'src/ui/terminal_window.cpp').
    *   `min_lines` *(integer, optional)*: Minimum line count threshold to filter out trivial inline declarations (default: 1).
    *   `full` *(boolean, optional)*: Whether to return un-truncated whole-file symbols and section headings (default: true).
    *   `max_symbols` *(integer, optional)*: Maximum symbol count cap (default: 0 for unlimited).

### `fs_grep_files`
*   **Description:** Search for a pattern (string or RE2 regular expression) across multiple files in the project. Use this instead of grep. Returns formatted markdown with line numbers, enclosing symbol context annotations (e.g. `[in function foo]`), and matches. Ideal for finding definitions, usages, or error messages across the codebase.
*   **Arguments:**
    *   `pattern` *(string, required)*: The RE2 regular expression to search for.
    *   `case_insensitive` *(boolean, optional)*: Set to true to ignore case during regex/literal matching. Defaults to false (case-sensitive search).
    *   `include_ext` *(string, optional)*: Filter by file extension (e.g., '.cpp', '.py').
    *   `path` *(string, optional)*: Restrict search to a specific file or directory path relative to project root. Defaults to the document root if omitted. (Alias for `search_path`.)
    *   `search_path` *(string, optional)*: Restrict search to a specific file or directory path relative to project root. Defaults to the document root if omitted.
    *   `exclude_path` *(string, optional)*: Filter out files/directories containing this path substring or prefix (e.g. 'build/', 'vendor/'). Optional.
    *   `exclude_ext` *(string, optional)*: Filter out files with specified extension(s), single (e.g. '.log') or comma-separated (e.g. '.log,.json'). Optional.
    *   `exclude_pattern` *(string, optional)*: Regex or substring pattern to exclude matching file paths or lines. Optional.
    *   `limit` *(integer, optional)*: Cap the total number of detailed matches to prevent blowing out the context window. Defaults to 50. If exceeded, only filenames are listed for the remaining matches.

### `fs_read_binary`
*   **Description:** Reads binary content from a file and returns it as a base64 encoded string or space-separated hex bytes. Can read a specific range using offset and size.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').
    *   `offset` *(integer, optional)*: The 0-based byte offset to start reading from. Defaults to 0.
    *   `size` *(integer, optional)*: The number of bytes to read. Defaults to reading the rest of the file if omitted. A maximum limit (e.g., 50MB) may apply.
    *   `format` *(string, optional)*: The output format (`'base64'` or `'hex'`). Defaults to `'base64'`.

### `fs_regexp_lines`
*   **Description:** Search for a regular expression within a file and return matching lines as a Markdown table.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').
    *   `pattern` *(string, required)*: The RE2 regular expression pattern to search for (e.g., `'function.*foo'`).
    *   `case_insensitive` *(boolean, optional)*: Set to true to ignore case during regex matching. Defaults to false.

### `fs_file_size`
*   **Description:** Get the size of a file in bytes.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').

### `fs_list_tests`
*   **Description:** Returns a markdown table of available test names in the project, optionally filtered by a pattern.
*   **Arguments:**
    *   `pattern` *(string, optional)*: Optional pattern (string or RE2 regular expression) to filter test names.

### `fs_glob`
*   **Description:** Returns a list of files matching a glob pattern (supporting double-star `**` wildcards) relative to the project root.
*   **Arguments:**
    *   `pattern` *(string, required)*: The glob pattern to search for, relative to the project root (e.g. `src/**/*.cpp` or `docs/*.md`).

### `fs_man`
*   **Description:** Lookup and render system man pages (library functions, system calls, or commands) as Markdown. Use this to find exact C/C++ function signatures, parameter names/types, required header files, return codes, and behavior of standard library APIs (e.g., malloc, printf, sockets, pthread) or system utilities.
*   **Arguments:**
    *   `name` *(string, required)*: The name of the function, library call, system call, or command to lookup (e.g., `'malloc'`, `'mmap'`, `'open'`, `'printf'`, `'pthread_create'`).
    *   `section` *(string, optional)*: Optional man page section (e.g., `"3"` for library functions, `"2"` for system calls, `"1"` for commands). If omitted, prioritizes library calls (section 3) first.
    *   `filter` *(string, optional)*: Optional. Extract only the portion of the rendered man page matching this directive or section name (e.g., `'ProtectKernelTunables'` or `'SANDBOXING'`). Use this to avoid returning a large man page when you only need a specific part.
    *   `output_path` *(string, optional)*: Optional relative file path under the project workspace or VFS URI (e.g., `'tmp://man.md'`) to save the rendered Markdown output to instead of returning it.

### `fs_man_search`
*   **Description:** Search system manual page names and descriptions for a keyword (similar to 'man -k' or 'apropos'). Returns matching commands, system calls, or library functions with their section numbers and descriptions in a markdown table.
*   **Arguments:**
    *   `query` *(string, required)*: The keyword or search phrase (e.g., `'socket'`, `'pthread'`, `'printf'`).
    *   `section` *(string, optional)*: Optional section to filter results (e.g., `"1"` for commands, `"2"` for system calls, `"3"` for library functions).

### `markdown_extract`
*   **Description:** Dispatches a specialized subagent to extract specific sections, directives, or topics from a Markdown document or VFS manpage (e.g. `system://man/systemd.exec.md`) with full section context and fidelity. Utilizes `fs_file_codemap` structural outlines and line-search tools internally. Part of the `base` tool family (always active).
*   **Arguments:**
    *   `path` *(string, required)*: Relative file path under the project workspace or VFS URI (e.g., `docs/design.md` or `system://man/systemd.exec.md`).
    *   `query` *(string, required)*: The specific topic, directive name (e.g., `ProtectKernelTunables`), question, or section heading to extract.
    *   `output_path` *(string, optional)*: Optional relative file path under the project workspace or VFS URI (e.g. `tmp://extract.md`) to save the extracted Markdown result.
    *   `async` *(boolean, optional)*: Optional. If true, runs extraction in the background. Default is false (synchronous extraction).

---

## 2. File System Mutation

*Note: All mutation tools enforce write-access permissions and strictly prevent modifying files currently active in an editor buffer to avoid race conditions.*

### `fs_replace_lines`
*   **Description:** Surgically edit a file by providing an array of line operations (add, remove, replace). Edits MUST be sorted in descending `line_number` order to prevent line-shifting offsets. Large edit blocks (> 10 lines) report head and tail context lines with omitted line counts to conserve context space.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Path to the file to edit.
    *   `edits` *(array of objects, required)*: A list of edit operations.
        *   `line_number` *(integer, required)*: The 1-based line number to target.
        *   `type` *(string, required)*: The type of edit operation (`add`, `remove`, `replace`).
        *   `original_text` *(string, optional)*: Required for `remove` and `replace`. The exact full content of the original line(s) being modified. You MAY provide multiple lines separated by \n to replace entire blocks of code. Used for safety verification. Pass empty string for `add`.
        *   `replace_with` *(string, optional)*: Required for `add` and `replace`. The new content to insert or replace the line with. You MAY use newline characters (\n) here to insert multiple lines. Pass empty string for `remove`.
    *   `strict` *(boolean, optional)*: If true, reject (and revert) the edits when they would leave braces unbalanced, instead of applying them and only issuing a warning. Defaults to false.

### `fs_replace_content`
*   **Description:** Edit a file by replacing a unique contiguous block of text (`target_content`) with a new block (`replacement_content`), avoiding line-shifting errors. Supports staged relaxed matching (whitespace/CRLF normalization) and LSP/symbol scope resolution using `function_hint` or `line_hint` if multiple matches exist.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Path to the file to edit.
    *   `target_content` *(string, required)*: The exact, contiguous block of text to replace in the file.
    *   `replacement_content` *(string, required)*: The new content that will replace `target_content`.
    *   `line_hint` *(integer, optional)*: A 1-based line number hinting where the block starts. Required to resolve ambiguity if the target content appears multiple times.
    *   `function_hint` *(string, optional)*: The name of the enclosing function, method, or class (e.g., `'execute'`). Highly recommended for long files to restrict search scope and resolve ambiguity.
    *   `start_line` *(integer, optional)*: A 1-based start line number establishing a search window boundary for target content.
    *   `end_line` *(integer, optional)*: A 1-based end line number establishing a search window boundary for target content.
    *   `strict` *(boolean, optional)*: If true, reject (and revert) the edit when it would leave braces unbalanced, instead of applying it and only issuing a warning. Defaults to false.

### `fs_multi_replace_content`
*   **Description:** Replaces multiple non-contiguous blocks of text across a single file in a single atomic transaction. If any chunk fails to match or breaks syntax/brace-balance in strict mode, all changes are cleanly rolled back.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Path to the file to edit.
    *   `chunks` *(array of objects, required)*: Array of replacement chunks to apply atomically.
        *   `target_content` *(string, required)*: The exact block of text in the file to replace.
        *   `replacement_content` *(string, required)*: The new text block that will replace `target_content`.
        *   `line_hint` *(integer, optional)*: A 1-based line number hinting where `target_content` is located.
        *   `function_scope` *(string, optional)*: Enclosing function/class/method name (e.g. `'validate_args_impl'`) to restrict search scope.
        *   `start_line` *(integer, optional)*: A 1-based start line boundary for search scope.
        *   `end_line` *(integer, optional)*: A 1-based end line boundary for search scope.
    *   `strict` *(boolean, optional)*: If true, reject (and revert) the edits if they leave braces unbalanced. Defaults to false.

### `fs_write_file`
*   **Description:** Creates a new file, overwrites an existing file, or safely appends content to an existing file.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Path to the file to write.
    *   `content` *(string, required)*: The entire complete content to write into the file, or content to append.
    *   `force_overwrite` *(boolean, optional)*: Set to true to overwrite an existing file. Defaults to false. Mutually exclusive with `append`.
    *   `append` *(boolean, optional)*: Set to true to safely append `content` to the end of an existing file. Defaults to false. Mutually exclusive with `force_overwrite`.

### `fs_mkdir`
*   **Description:** Create a directory, including any necessary parent directories (like mkdir -p). Supports directory paths relative to the project root, or virtual paths (e.g., `tmp://nested/dir`).
*   **Arguments:**
    *   `path` *(string, required)*: Relative directory path under the project workspace or VFS URI (e.g., 'tmp://dir'). Path to the directory to create.

### `fs_purge_tmp`
*   **Description:** Purges (deletes) files and directories in the virtual `tmp://` scratch space. If a substring is provided, only deletes files/directories whose names contain the substring.
*   **Arguments:**
    *   `substring` *(string, optional)*: Only delete files containing this substring in their name/path.

---

## 3. Compilation & Diagnostics

### `fs_run_tests`
*   **Description:** Runs the project's test suite (synchronously) and returns the console output. Catch crashes and dumps backtraces. Runs with terminal interaction.
*   **Arguments:** None.

### `fs_compile_project`
*   **Description:** Compiles the entire project and returns the raw console output. Populates the workspace error list. Can be run asynchronously.
*   **Arguments:**
    *   `clean` *(boolean, optional)*: If true, forces a completely clean rebuild before compiling to clear out stale artifacts. Defaults to false.
    *   `async` *(boolean, optional)*: If true, runs the compilation asynchronously in the background. Defaults to false.
    *   `timeout` *(integer, optional)*: Optional compilation timeout in seconds. Defaults to 600.

### `fs_compile_file`
*   **Description:** Compiles a single file and returns the raw console output. Populates the workspace error list. Can be run asynchronously. NOTE: This only compiles the individual file (e.g. checking syntax/errors) but does NOT link the project, so the executable binary will NOT be updated. To rebuild/link the whole project binary, use `fs_compile_project`.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file to compile, relative to the project root.
    *   `async` *(boolean, optional)*: If true, runs the compilation asynchronously in the background. Defaults to false.

### `fs_compile_info`
*   **Description:** Retrieves the exact compile command (from `compile_commands.json`), the last compile time, and any active build/LSP diagnostics for a specific file.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.

---

## 4. UI Overlays & Feedback

### `flag_as_error`
*   **Description:** Flags a specific line (and optional column range) in a file as an error or warning, creating a diagnostic overlay in the editor UI (red/yellow backgrounds).
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').
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

### `open_in_editor`
*   **Description:** Open a file in the editor UI for the user to view or edit. If the file is already open in a window, that window is activated and focused; otherwise, the file is loaded in a new window.
*   **Arguments:**
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt').

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

### `activate_tool_family`
*   **Description:** Activates a specialized tool family by name. This makes all tools belonging to that family available in the agent's context. By default, only the 'base' family is active.
*   **Arguments:**
    *   `name` *(string, required)*: The name of the tool family to activate.

### `get_current_datetime`
*   **Description:** Returns the current date and time as a markdown table. Includes Unix time, Year, Month, Day, Hour, Minute, Second, and Timezone.
*   **Arguments:** None.

### Tool, Skill & State Discovery via `system://` VFS
Tool discovery, skill discovery, and workspace diagnostic summaries are performed via the Virtual Filesystem (`system://`) scheme rather than function calls:
*   `system://tools.md` (or `system://tools.md?search=pattern`): Reads a Markdown table summarizing all active tools (`| Tool Name | Description |`).
*   `system://tools_detailed.md` (or `system://tools/details.md`): Reads full parameter schemas (types, descriptions, required fields) for deep inspection.
*   `system://skills.md` (or `system://skills.md?search=pattern`): Reads a Markdown table of all available agent skills, URIs, and descriptions (replacing `list_skills`).
*   `system://project/diagnostics.md` (or `system://project/summary.md`, `system://diagnostics.md`): Reads a Markdown summary of compiler errors, compiler warnings, and LSP diagnostics across workspace files (replacing `fs_compile_summary`).
*   `system://project/info.md` (or `system://project/overview.md`): Reads project workspace overview details, including root path, build system type, and instruction files (`GEMINI.md`, `AGENTS.md`).

### `run_python`
*   **Description:** Executes Python code in a sandboxed environment.
*   **Arguments:**
    *   `code` *(string, optional)*: The raw Python code string to execute.
    *   `path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://script.py'). Path to a Python script to execute.
    *   `dependencies` *(array of strings, optional)*: PyPI dependencies to temporarily install via 'uv' (if available). If a `venv` is also provided, they are installed into that virtual environment instead.
    *   `venv` *(string, optional)*: Path to a Python virtual environment directory (e.g. '.venv'). Its interpreter (`<venv>/bin/python`) is used to run the script, and any `dependencies` are installed into it. Resolved relative to the project root.

### `web_fetch`
*   **Description:** Fetches content from a URL via HTTP/HTTPS. Useful for reading documentation or external resources. Implements domain-based access controls and prompts the user for permission.
*   **Arguments:**
    *   `url` *(string, required)*: The full URL to fetch (must start with http:// or https://).
    *   `method` *(string, optional)*: Optional HTTP method to use (e.g. `GET`, `POST`, `PUT`, `DELETE`, `HEAD`). Defaults to `GET`.
    *   `headers` *(object, optional)*: Optional custom HTTP request headers as key-value pairs (e.g. `{"Authorization": "Bearer token", "Content-Type": "application/json"}`).
    *   `output_path` *(string, optional)*: Optional relative file path under the project workspace or VFS URI (e.g. `tmp://file.txt`) to save the fetched content directly to disk.
    *   `filter` *(string, optional)*: Optional content processing filter to apply before returning or saving (e.g. `html_to_markdown`, `html_to_markdown_plain`, `html_extract_tables`).
    *   `no_ask` *(boolean, optional)*: If true, the tool will fail silently with a permission error if the domain is not pre-approved, rather than prompting the user for permission.


### `apply_text_filter`
*   **Description:** Applies a named content processing or format conversion filter (e.g., converting HTML to Markdown via 'html_to_markdown', aligning tables via 'markdown_align_tables', or sanitizing input via 'strip_ansi'/'strip_utf8') to input text or a workspace/VFS file. Optionally saves the converted output directly to a workspace file.
*   **Arguments:**
    *   `filter` *(string, required)*: The name of the filter to apply (e.g., `strip_utf8`, `strip_ansi`, `html_to_markdown`, `html_to_markdown_plain`, `html_extract_tables`, `markdown_align_tables`, `meson_compile`, `meson_test`).
    *   `text` *(string, optional)*: The input text string to convert or filter. Either `text` or `path` must be provided.
    *   `path` *(string, optional)*: Optional relative file path under the project workspace or VFS URI (e.g. `tmp://page.html`) to read input text from.
    *   `output_path` *(string, optional)*: Optional relative file path under the project workspace or VFS URI (e.g. `tmp://out.md`) to save the converted output directly to disk.

### `run_shell_command`
*   **Description:** Runs an arbitrary shell command safely within the sandbox. Requires explicit user permission approval and interrupts the agent flow. Do NOT use run_shell_command to read files (use fs_read_lines), search code (use fs_grep_files or fs_find_files), list tests (read system://project/testlist.md via fs_read_lines), run unit tests (use fs_run_tests), or check git status/diff (use git_status, git_diff_unstaged, git_log).
*   **Arguments:**
    *   `command` *(string, required)*: The exact shell command to execute.
    *   `timeout` *(integer, optional)*: Optional timeout in seconds. Default is 300.
    *   `async` *(boolean, optional)*: Optional. If true, runs the command in the background. Default is false.
    *   `force` *(boolean, optional)*: Set to true ONLY if native specialized tools are genuinely insufficient and you require explicit user approval for a raw shell command.

---

## 7. Agent State & To-Do Management

### `agent_mark_milestone`
*   **Description:** Used to signal that a major task is complete or that you are pivoting to a completely new area. This helps the system manage long-term memory and context windows efficiently by compressing old history. Upon success, the tool response includes a reminder message listing the number of remaining outstanding todos and the text of the next todo item (repetition-limited to 2 reminders per item).
*   **Arguments:**
    *   `title` *(string, required)*: A short title for the completed task or the new milestone.
    *   `summary` *(string, required)*: A concise summary of the work that was just completed and the goal of the new phase.

### `agent_compress_history`
*   **Description:** Proactively pages out conversational history prior to this tool call into a saved milestone archive. This frees up your context window. A highly dense pointer message replaces the old history, allowing you to restore it later if needed. *Note: Only available if the active model supports history mutation.*
*   **Arguments:**
    *   `title` *(string, required)*: A short title for the milestone you are archiving.
    *   `summary` *(string, required)*: A concise summary of the history being paged out.
    *   `tags` *(array of strings, optional)*: Semantic tags to label this archive.
    *   `target_episode_id` *(string, optional)*: The exact ID of the milestone or system message (e.g., 'episode_123') that acts as the UPPER boundary. If omitted, pages out the active/current block.
    *   `include_all_prior` *(boolean, optional)*: If true, ignores the lower boundary and compresses everything from the target back to the system prompt.

### `agent_restore_context`
*   **Description:** Pages in a previously saved context archive (episode). Use this if you need to resume work on an old task or look up historical context. Find the episode_id by using the '/memory' command or reading the SYSTEM MEMORY pointers in your history. *Note: Only available if the active model supports history mutation.*
*   **Arguments:**
    *   `episode_id` *(string, required)*: The exact ID of the episode to restore.
    *   `compression_level` *(integer, optional)*: Controls how aggressively the archive is optimized during restoration. `0` = Raw history. `1` = Think-Free reasoning stripping (default). `2` = Terminal truncation / active level 2. Defaults to `1`.

### `agent_list_episodes`
*   **Description:** Lists all archived/paged-out episodes, returning a markdown table showing the Episode ID and their 'when to resume' reactivation hint.
*   **Arguments:** None.

### `agent_set_timer`
*   **Description:** Sets a timer (in seconds) that runs in the background. Once the timer expires, if the agent is idle, it injects a `"previously set timer expired"` system message to wake the agent.
*   **Arguments:**
    *   `seconds` *(integer, required)*: The duration of the timer in seconds.

---

## 8. Subagent Orchestration

### `invoke_subagent`
*   **Description:** Invokes a subagent to delegate tasks to. You must provide a `subagent_name`, a `task` (user request), or a `profile` (system instructions), or a combination of them.
*   **Arguments:**
    *   `name` *(string, required)*: A short, descriptive name for the subagent.
    *   `subagent_name` *(string, optional)*: Optional name of a pre-configured subagent profile.
    *   `profile` *(string, optional)*: System instructions and personality profile for the subagent.
    *   `task` *(string, optional)*: The initial task or request for the subagent to perform.
    *   `repository_url` *(string, optional)*: Optional git repository URL to pre-seed the remote A2A subagent workspace before execution. If omitted during remote A2A invocation, auto-defaults to the active project repository URL.
    *   `git_ref` *(string, optional)*: Optional branch or commit hash to checkout when pre-seeding the repository.
    *   `wait` *(boolean, optional)*: If true, the tool will wait for the subagent to complete its task and will return its final response directly. Defaults to false (asynchronous).
    *   `local_only` *(boolean, optional)*: If true, strictly restricts execution to local subagents. Defaults to false.

### `a2a_connect_server`
*   **Description:** Connects to a remote A2A server and registers it for remote subagent invocation (`invoke_subagent`).
*   **Arguments:**
    *   `name` *(string, required)*: Unique short name for the server (e.g. `devpc` or `gpu_node`).
    *   `url` *(string, required)*: Base URL of the A2A server (e.g. `http://devpc.local:7820`).
    *   `auth_token` *(string, optional)*: Optional bearer token or API key for authentication.
    *   `persistent` *(boolean, optional)*: If true, saves server connection to project-local settings (`.cache/turbostar/projects/<hash>/a2a_servers.json`). Defaults to false (ephemeral).

### `list_subagents`
*   **Description:** Lists all active subagents managed by the current agent. Returns a Markdown table of ID, Name, and Status.
*   **Arguments:** None.

### `get_subagent_status`
*   **Description:** Returns detailed status information about a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to query.

### `send_message`
*   **Description:** Sends a message or command to an active subagent, appending it to their processing queue.
*   **Security:**
    *   Maximum message length: 100KB (prevents DoS attacks)
    *   Validates target agent exists and is a direct child of the calling agent
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent.
    *   `message` *(string, required)*: The text message or instruction to send (max 100KB).

### `wait_for_subagent`
*   **Description:** Pauses execution until the specified subagent becomes idle.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to wait for.

### `get_subagent_output`
*   **Description:** Retrieves the entire interaction history of a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to query.

### `report_final_result`
*   **Description:** Reports the final result or summary of the completed task back to the parent agent. This replaces the default full interaction history returned to the parent with only the reported final result.
*   **Arguments:**
    *   `result` *(string, required)*: The final result or outcome to report to the parent.

### `kill_subagent`
*   **Description:** Closes and terminates a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to terminate.

---

## 9. Crashdump & Crash Analysis

### `crashdump_list`
*   **Description:** Returns a markdown table of recent crashdumps (crashes) generated by commands in the sandbox (up to limit, returning the most recent ones).
*   **Arguments:**
    *   `limit` *(integer, optional)*: Optional limit on the maximum number of recent crash dumps to return. Defaults to 20.

### `crashdump_get_info`
*   **Description:** Retrieves the detailed backtrace and GDB analysis of a specific crashdump.
*   **Arguments:**
    *   `pid` *(integer, required)*: The Process ID (PID) of the crashed executable.

### `crashdump_clear`
*   **Description:** Deletes all crash dumps from the disk and clears the internal crash dump list. Use this to remove stale crash dumps after they have been investigated.
*   **Arguments:** None.

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

---

## 11. Git Operations

These tools allow the agent to interact with the project's Git repository.

### `git_status`
*   **Description:** Get the git status of the project repository as a Markdown table (shows staged, unstaged, and untracked files).
*   **Arguments:** None.

### `git_list_files`
*   **Description:** List all tracked files in the Git repository index under a specified path or directory as a 2-column Markdown table (`| File Path | Status (blank=Tracked) |`). Clean tracked files have a blank status cell; modified/deleted files are annotated (`MOD`, `DEL`, `UNM`). ALWAYS use this tool to query tracked git files instead of running 'git ls-files' via run_shell_command.
*   **Arguments:**
    *   `path` *(string, optional)*: Relative path or directory under project root (defaults to '.').
    *   `pattern` *(string, optional)*: Optional pattern or substring to filter filenames (e.g. '.cpp' or 'src/').
    *   `limit` *(integer, optional)*: Maximum number of files to return (defaults to 500, max 5000).

### `git_add`
*   **Description:** Stages specific files or directories for the next commit (git add <paths>).
*   **Arguments:**
    *   `paths` *(array of strings, required)*: List of paths relative to the project root to stage (e.g., ['src/main.cpp', 'docs/']).

### `git_unstage`
*   **Description:** Unstage files that have been added to the Git index (git reset HEAD <paths>). Does not discard local file changes.
*   **Arguments:**
    *   `paths` *(array of strings, required)*: List of paths relative to the project root to unstage.

### `git_commit`
*   **Description:** Commit the currently staged changes with the provided commit message.
*   **Arguments:**
    *   `message` *(string, required)*: The commit message.

### `git_diff_unstaged`
*   **Description:** View the uncommitted/unstaged git diff for a specific file or directory (use '.' for the entire project). Use this instead of running 'git diff' via the shell. Returns raw patch output.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file or directory to diff, relative to the project root.

### `git_diff_staged`
*   **Description:** View the staged git diff for a specific file or directory (use '.' for the entire project). Use this instead of running 'git diff --staged' or 'git diff --cached' via the shell. Returns raw patch output.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file or directory to diff, relative to the project root.

### `git_log`
*   **Description:** View the last commit messages in the repository (git log -n <limit> --oneline).
*   **Arguments:**
    *   `limit` *(integer, optional)*: The maximum number of commits to retrieve. Defaults to 10.

### `git_blame`
*   **Description:** View the commit-level git blame history of a file, consolidated into contiguous ranges of lines with commit summary and date. Grounding code is provided for the start line of each range to assist the agent.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.
    *   `start_line` *(integer, optional)*: Optional 1-based start line. Defaults to 1.
    *   `end_line` *(integer, optional)*: Optional 1-based end line. Defaults to the end of the file.

### `git_branch_list`
*   **Description:** List all git branches in the repository as a Markdown table, indicating the currently active branch.
*   **Arguments:** None.

### `git_branch_create`
*   **Description:** Create a new git branch from the current HEAD.
*   **Arguments:**
    *   `branch_name` *(string, required)*: The name of the new branch to create.

### `git_checkout_branch`
*   **Description:** Switch to an existing git branch (git checkout <branch>).
*   **Arguments:**
    *   `branch_name` *(string, required)*: The name of the branch to switch to.

### `git_diff_from_branch`
*   **Description:** Compare the current working tree against another branch (git diff <branch>). Returns raw patch output.
*   **Arguments:**
    *   `branch_name` *(string, required)*: The name of the branch to compare against.

### `git_pull`
*   **Description:** Synchronize the current branch with the remote (git pull).
*   **Arguments:** None.

### `git_push`
*   **Description:** Push the current branch to the remote repository. Note: force pushing requires explicit user approval.
*   **Arguments:**
    *   `force` *(boolean, optional)*: Whether to force push.

### `git_restore`
*   **Description:** Discard uncommitted local changes to a file or directory (git checkout/restore <path>). Does not affect staged files.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file or directory to restore.

### `git_init`
*   **Description:** Initialize a new Git repository in the current project root. Fails if a .git directory already exists.
*   **Arguments:** None.

## 8. Agent Mode Management

### `enter_plan_mode`
*   **Description:** Switch to Plan Mode to safely research, design, and plan complex changes using read-only tools.
*   **Arguments:**
    *   `reason` *(string, optional)*: Short reason explaining why you are entering plan mode.

### `exit_plan_mode`
*   **Description:** Exit Plan Mode and request user approval for the finalized plan. Upon approval, modifying tools will be unlocked.
*   **Arguments:**
    *   `plan_title` *(string, required)*: A short title for the plan (1-5 words).
    *   `plan_summary` *(string, required)*: The complete, step-by-step finalized plan to present to the user.
    *   `page_out_history` *(boolean, optional)*: If true, compresses all exploratory work done since entering Plan Mode into a single milestone on disk, leaving only the plan in the active context window to save tokens. Strongly recommended.

## 9. Interactive Execution & Debugging

### `agent_set_application_binary`
*   **Description:** Sets the main application binary/executable path for run and debug operations. Note: The path must be specified relative to the `build/` directory (e.g., `'turbostar'` or `'test_tool_infrastructure'`).
*   **Arguments:**
    *   `path` *(string, required)*: The path to the main application executable, relative to the `build/` directory.

### `agent_start_app`
*   **Description:** Starts the main application executable, optionally under GDB debugging with split screen or CPU performance sampling. Returns JSON with `app_run_id` and `gdb_run_id`.
*   **Arguments:**
    *   `args` *(string, optional)*: Command line arguments to pass to the application.
    *   `debugger` *(boolean, optional)*: If true, starts the application with a split screen debugger (GDB/GDBServer). Defaults to false.
    *   `wait_for_time` *(integer, optional)*: Optional time in seconds to wait for the application to finish or exit after starting. Defaults to 0 (async execution).
    *   `collect_performance` *(boolean, optional)*: If true, enables CPU cycle performance profiling sampling via `LD_PRELOAD` during execution. Defaults to false.

### `agent_get_profile_summary`
*   **Description:** Returns top functions and source lines ranked by CPU cycle percentage from the active or specified performance profile run.
*   **Arguments:**
    *   `run_id` *(string or integer, optional)*: The execution run ID returned by `agent_start_app` (e.g. `'run_1'`, `1`, or `'editor'`). If omitted or empty, returns the active profile from the most recent run.
    *   `limit` *(integer, optional)*: Maximum number of top functions and lines to return. Defaults to 10.

### `agent_get_profile_details`
*   **Description:** Returns line-by-line performance profiling details with source code text, line numbers sorted ascending, ±2 context lines merged into continuous blocks, sample counts, global application cycle percentages (`global_percentage`), and function/file cycle percentages (`function_percentage` when filtering by function, `file_percentage` when filtering by file) for a target source file or function name.
*   **Arguments:**
    *   `run_id` *(string or integer, optional)*: The execution run ID returned by `agent_start_app` (e.g. `'run_1'`, `1`, or `'editor'`). If omitted or empty, returns details for the active profile from the most recent run.
    *   `path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Source file path to filter line performance samples.
    *   `function_name` *(string, optional)*: Function name to filter line performance samples.

### `agent_wait_for_app`
*   **Description:** Waits until a running process has either ended/crashed or reached a settled state without output for at least 500ms. Returns JSON with execution status, `is_alive`, `age_ms`, and optional `crash_notification`.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.
    *   `type` *(string, optional)*: The wait condition: `'ended'` (default) waits for process termination or crash, `'settled'` waits for either termination or 500ms of no output.
    *   `timeout_sec` *(integer, optional)*: Maximum time in seconds to wait before returning status `'timeout'`. Defaults to 30.

### `agent_debug_coredump`
*   **Description:** Launches a GDB session attached to the coredump for a given crash_id. Returns JSON containing the `gdb_run_id` and detailed interactive `instructions` (which guide command inputs and warn to call `agent_terminate_run` when finished).
*   **Arguments:**
    *   `crash_id` *(string, required)*: The unique crash ID from the crash database to debug.

### `agent_write_to_run`
*   **Description:** Writes/injects keyboard input sequences into the application or debugger PTY master stream.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.
    *   `data` *(string, required)*: The raw string data or escape sequence to inject.
    *   `output` *(boolean, optional)*: If true, records terminal output, waits for the terminal state to settle, and returns the recorded text in the tool response. Defaults to false.

### `agent_get_run_screenshot`
*   **Description:** Returns a snapshot/screenshot of the terminal buffer grid, cursor coordinates, process alive status (`is_alive`), and optional `crash_notification` for a given run ID.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.
    *   `settle` *(boolean, optional)*: If true, waits up to 3 seconds for terminal content to settle before capturing screenshot.

### `agent_terminate_run`
*   **Description:** Terminates/stops a running process and closes its window based on its run ID.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.

---

## 10. Specialized Tool Families

### `x86_disassemble` (Family: `x86`)
*   **Description:** Disassembles raw x86/x64 machine code bytes into human-readable assembly instructions. Outputs a Markdown table containing the instruction bytes and the formatted instruction.
*   **Arguments:**
    *   `data` *(string, required)*: The raw machine code bytes to disassemble. Can be Base64 or space-separated ASCII hex.
    *   `format` *(string, optional)*: The input format (`'hex'`, `'base64'`, or `'auto'`). Defaults to `'auto'`.
    *   `mode` *(string, optional)*: The CPU mode (`'16'`, `'32'`, or `'64'`). Defaults to `'64'`.
    *   `syntax` *(string, optional)*: Assembly syntax format (`'intel'` or `'att'`). Defaults to `'intel'`.
    *   `address` *(integer, optional)*: The starting runtime address/IP offset. Defaults to 0.

### `x86_assemble` (Family: `x86`)
*   **Description:** Assembles a single x86/x64 assembly instruction string into its corresponding machine code bytes (space-separated hex).
*   **Arguments:**
    *   `instruction` *(string, required)*: The assembly instruction to assemble (e.g. `'mov eax, eax'`).
    *   `mode` *(string, optional)*: The CPU mode (`'16'`, `'32'`, or `'64'`). Defaults to `'64'`.
    *   `syntax` *(string, optional)*: Assembly syntax format (`'intel'` or `'att'`). Defaults to `'intel'`.

### `hexdump` (Family: `hexedit`)
*   **Description:** Generates a formatted hexadecimal dump of a binary file. Annotates output with binary structure details (like ELF or PNG segments) automatically using registered syntax highlighters.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the binary file relative to project root.
    *   `start_offset` *(integer, optional)*: 0-based byte offset to start the dump. Defaults to 0.
    *   `size` *(integer, optional)*: Number of bytes to dump. Defaults to 256. Maximum 4096.

### `hexwrite` (Family: `hexedit`)
*   **Description:** Overwrites/patches raw bytes at a specific offset in a binary file. Creates a new file or expands an existing file if the offset exceeds the current file size.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the binary file relative to project root.
    *   `data` *(string, required)*: Hexadecimal bytes to write, optionally space-separated or with prefix (e.g. `'0x7f 0x45'` or `'7f 45'`).
    *   `offset` *(integer, optional)*: 0-based byte offset to write the data. Defaults to 0.

### `hexinspect` (Family: `hexedit`)
*   **Description:** Inspects the semantic/structural details of a binary file range using registered syntax highlighters (like ELF or PNG). Returns a markdown list of fields, offsets, sizes, and annotations in the range.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the binary file relative to project root.
    *   `start_offset` *(integer, optional)*: 0-based byte offset to start inspecting. Defaults to 0.
    *   `size` *(integer, optional)*: Number of bytes to inspect. Defaults to 256. Maximum 4096.

### `html_extract_tables` (Family: `html`)
*   **Description:** Parses an HTML file, extracts all tables, and formats them as beautifully aligned markdown tables. Prepends active heading structures (H1, H2, H3) above each table automatically.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the HTML file relative to project root.
    *   `output_path` *(string, optional)*: Optional path relative to project root to write the markdown output file to.

### `html_list_links` (Family: `html`)
*   **Description:** Extracts all hyperlink anchor text and URLs from an HTML document and formats them into an aligned markdown table.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the HTML file relative to project root.

### `html_list_images` (Family: `html`)
*   **Description:** Extracts all image alt texts and source URLs from an HTML document and formats them into an aligned markdown table.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the HTML file relative to project root.

### `html_extract_text` (Family: `html`)
*   **Description:** Extracts structured text from an HTML document as Markdown, keeping lists, headers, code blocks, tables, and links.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the HTML file relative to project root.
    *   `rich` *(boolean, optional)*: If true (default), inline elements (bold/italic) will be preserved in Markdown format. If false, they will be stripped.

### `image_import` (Family: `image`)
*   **Description:** Imports an image from a local file or a web URL into the virtual VFS image database.
*   **Arguments:**
    *   `filename` *(string, optional)*: Path to a local image file relative to the project root.
    *   `URL` *(string, optional)*: HTTP/HTTPS URL of the image to download.
    *   `output` *(string, required)*: The alias name to assign to the imported image (e.g. `logo.png`).

### `image_export` (Family: `image`)
*   **Description:** Exports an image from the virtual image database to a real file in the workspace.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the image to export.
    *   `filename` *(string, required)*: The destination path relative to the project root where the file will be saved.

### `image_resize` (Family: `image`)
*   **Description:** Resizes an image to target dimensions or by a scaling ratio. If output is specified, saves the resized image as a new image alias; otherwise, resizes in place.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the image (e.g. `images://by-name/image.png` or `image.png`).
    *   `newX` *(integer, optional)*: Optional new width in pixels.
    *   `newY` *(integer, optional)*: Optional new height in pixels.
    *   `ratio` *(number, optional)*: Optional scaling ratio (e.g. 0.5 to shrink to 50%).
    *   `output` *(string, optional)*: Optional new alias name or VFS URI to save the resized image as.

### `image_crop` (Family: `image`)
*   **Description:** Crops a rectangular selection from an image. If output is specified, saves the cropped selection as a new image alias; otherwise, crops in place.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the source image.
    *   `width` *(integer, required)*: Width of the crop selection in pixels.
    *   `height` *(integer, required)*: Height of the crop selection in pixels.
    *   `x` *(integer, required)*: X coordinate offset of the selection.
    *   `y` *(integer, required)*: Y coordinate offset of the selection.
    *   `output` *(string, optional)*: Optional new alias name or VFS URI to save the cropped image as.

### `image_rotate` (Family: `image`)
*   **Description:** Rotates an image counter-clockwise by specified degrees. If output is specified, saves the rotated image as a new image alias; otherwise, rotates in place.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the image.
    *   `degrees` *(number, required)*: The number of degrees to rotate counter-clockwise.
    *   `output` *(string, optional)*: Optional new alias name or VFS URI to save the rotated image as.

### `image_mirror` (Family: `image`)
*   **Description:** Mirrors (flips/flops) an image horizontally, vertically, or both. If output is specified, saves the mirrored image as a new image alias; otherwise, mirrors in place.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the image.
    *   `direction` *(string, optional)*: The direction to mirror: `'horizontal'` (flop), `'vertical'` (flip), or `'both'`. Default is `'horizontal'`.
    *   `output` *(string, optional)*: Optional new alias name or VFS URI to save the mirrored image as.

### `image_grayscale` (Family: `image`)
*   **Description:** Converts an image to grayscale. If output is specified, saves the grayscale image as a new image alias; otherwise, converts in place.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the image.
    *   `output` *(string, optional)*: Optional new alias name or VFS URI to save the grayscale image as.

### `image_threshold` (Family: `image`)
*   **Description:** Applies standard or adaptive thresholding (binarization) to an image. If output is specified, saves the binarized image as a new image alias; otherwise, binarizes in place.
*   **Arguments:**
    *   `name` *(string, required)*: The name alias or VFS URI of the image.
    *   `level` *(number, optional)*: Optional standard threshold level. If specified, standard binarization thresholding is used. If omitted, local adaptive thresholding is performed.
    *   `windowWidth` *(integer, optional, default: 16)*: Neighborhood window width for adaptive thresholding.
    *   `windowHeight` *(integer, optional, default: 16)*: Neighborhood window height for adaptive thresholding.
    *   `offset` *(number, optional, default: 0.0)*: Local constant subtraction offset for adaptive thresholding.
    *   `output` *(string, optional)*: Optional new alias name or VFS URI to save the binarized image as.

### `image_compose` (Family: `image`)
*   **Description:** Composes (overlays) a small image onto a main destination image at the specified x, y coordinates. If output is specified, saves the composed result as a new image alias; otherwise, modifies the main image in place.
*   **Arguments:**
    *   `main_image` *(string, required)*: The name alias or VFS URI of the destination/main image.
    *   `small_image` *(string, required)*: The name alias or VFS URI of the source/small image to overlay.
    *   `x` *(integer, required)*: X coordinate offset to place the small image on the main image.
    *   `y` *(integer, required)*: Y coordinate offset to place the small image on the main image.
    *   `output` *(string, optional)*: Optional new friendly alias name or VFS URI to save the composed image as.

### `image_getdata` (Family: `image`)
*   **Description:** Retrieves the binary content of a VFS image, returned as a Base64-encoded Data URL entity. Optionally returns an ephemeral thumbnail to keep the payload small.
*   **Arguments:**
    *   `filename` *(string, required)*: The friendly alias name or full VFS URI of the image.
    *   `max_bytes` *(integer, optional, default: 51200)*: Maximum allowed byte size for returned image data.
    *   `thumbnail` *(boolean, optional, default: false)*: If true, returns an ephemeral thumbnail with the largest dimension shrunk to 96px (aspect ratio preserved). This is a pure read: the VFS image is left unchanged and the thumbnail is computed on-the-fly and never persisted.

### `elf_list_sections` (Family: `x86`)
*   **Description:** Lists all section headers of an ELF file, providing their index, name, type, offset, size, and address mapping.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the ELF file relative to project root.

### `elf_list_symbols` (Family: `x86`)
*   **Description:** Lists all symbols in the symbol table of an ELF file, providing their name, offset/value, and size. Allows optional case-insensitive substring or regex filtering via the 'pattern' argument.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the ELF file relative to project root.
    *   `pattern` *(string, optional)*: Optional substring or regex pattern to filter symbol names.

---

## 11. Code Review Tools

### `create_code_review_item`
*   **Description:** Creates a new code review item in the database. When called by a subagent, a single compact line is appended to the parent agent's context (format: `#ID (severity): file:line - summary`) unless the calling agent has parent-injection suppressed (e.g. inside a synchronous `perform_code_review`, where results arrive via the toolcall return instead).
*   **Arguments:**
    *   `summary` *(string, required)*: Brief one-line summary of the issue.
    *   `path` *(string, required)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). File containing the issue.
    *   `line_number` *(integer, optional)*: 1-based line number target. Defaults to 0 if omitted.
    *   `line_content` *(string, optional)*: Exact content of the line. If omitted but line_number is given, it will be auto-resolved from the file.
    *   `severity` *(string, required)*: Severity of the issue (`'nit'`, `'low'`, `'medium'`, `'high'`, `'critical'`).
    *   `description` *(string, required)*: Detailed description of the bug or code quality issue.
    *   `proposed_fix` *(string, required)*: Clear step-by-step description of the proposed solution.

### `update_code_review_item`
*   **Description:** Updates fields (state, severity, description, proposed_fix) of an existing code review item.
*   **Arguments:**
    *   `id` *(integer, required)*: The unique ID of the code review item.
    *   `state` *(string, optional)*: The new state (`'invalid'`, `'new'`, `'confirmed'`, `'disputed'`, `'stale'`, `'resolved'`, `'verified-fixed'`).
    *   `severity` *(string, optional)*: The new severity.
    *   `description` *(string, optional)*: Updated description.
    *   `proposed_fix` *(string, optional)*: Updated proposed fix.

### `confirm_code_review_item`
*   **Description:** Confirms the correctness of a code review item (new -> confirmed) or verifies its resolution (resolved -> verified-fixed). Only accessible by the verifier role.
*   **Arguments:**
    *   `id` *(integer, required)*: The unique ID of the code review item to confirm/verify.

### `resolve_code_review_item`
*   **Description:** Resolves a code review item by marking its state as 'resolved' and recording the commit hash where the fix was implemented. Only accessible by developer and verifier roles.
*   **Arguments:**
    *   `id` *(integer, required)*: The unique ID of the code review item to resolve.
    *   `commit_hash` *(string, required)*: The git commit hash containing the resolution/fix.

### `list_code_review_items`
*   **Description:** Lists all code review items as a compact Markdown table. Normal agents only see unresolved items; verifiers can also see resolved items by setting `include_resolved` to true.
*   **Arguments:**
    *   `path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Optional file path prefix to filter items.
    *   `severity` *(string, optional)*: Optional severity filter (one of: `nit`, `low`, `medium`, `high`, `critical`). Specifying a level returns all items of that severity or more severe.
    *   `include_resolved` *(boolean, optional)*: If true, lists resolved/verified items (restricted to verifiers).

### `get_code_review_item`
*   **Description:** Retrieves the full JSON details of a specific code review item by its unique ID. Access to resolved items is restricted to the verifier role.
*   **Arguments:**
    *   `id` *(integer, required)*: The unique ID of the code review item.

### `security_review_with_agent`
*   **Description:** Spawns a dedicated security code review subagent (equipped with security scanning tools) to perform an audit of a set of files.
*   **Arguments:**
    *   `files` *(array of strings, required)*: List of file paths relative to the project root to perform security code review on.
    *   `instructions` *(string, optional)*: Optional custom instructions or specific focus areas for the security agent.
    *   `output_path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://findings.md'). Optional file path where the final markdown findings will be written.

---

## 12. Binary Compression Tools

### `data_compress` (Family: `binary`)
*   **Description:** Compresses data into various formats (`zstd`, `gzip`, `zlib`, `deflate`, `xz`, `bzip2`, `lz4`). Supports reading from an input file path / VFS URI or raw data strings. Output can be returned as text, hex, base64, or written directly to a file.
*   **Arguments:**
    *   `path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., `tmp://file.bin`) of the file to compress. Mutually exclusive with `input_data`.
    *   `input_data` *(string, optional)*: Raw data string, hex, base64, or data URL to compress. Mutually exclusive with `path`.
    *   `format` *(string, optional)*: Compression format (one of: `zstd`, `gzip`, `zlib`, `deflate`, `xz`, `bzip2`, `lz4`). Defaults to `zstd`.
    *   `output_format` *(string, optional)*: Format to return the output (one of: `hex`, `base64`, `text`). Defaults to `hex`.
    *   `output_path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., `tmp://file.bin`). Optional file path to write output instead of returning it.

### `data_decompress` (Family: `binary`)
*   **Description:** Extracts and decompresses data from various sources (file paths, VFS URIs, data URLs, hex/base64/ascii85 strings). Supports offset and size ranges to extract nested streams. Output can be returned as text, hex, base64, or written directly to a file.
*   **Arguments:**
    *   `path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., `tmp://file.bin`) of the file to decompress. Mutually exclusive with `input_data`.
    *   `input_data` *(string, optional)*: Raw data string, hex, base64, or data URL to decompress. Mutually exclusive with `path`.
    *   `format` *(string, optional)*: Compression format (one of: `auto`, `zstd`, `gzip`, `zlib`, `deflate`, `xz`, `bzip2`, `lz4`, `pdflzw`, `lzw`, `pdfrunlength`, `runlength`, `ascii85`, `none`). Defaults to `auto`.
    *   `output_format` *(string, optional)*: Format to return the output (one of: `hex`, `base64`, `text`). Defaults to `text`.
    *   `output_path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., `tmp://out.txt`). Optional file path to write output instead of returning it.
    *   `offset` *(integer, optional)*: Byte offset to start reading from. Defaults to 0.
    *   `size` *(integer, optional)*: Maximum number of bytes to read. Defaults to -1 (read all).

---

## 13. Agent-to-Agent (A2A) Tools

### `a2a_validate_card` (Family: `a2a`)
*   **Description:** Validates an A2A Agent Card JSON file or raw string against the formal A2A Agent Card specification. Returns a structured Markdown validation report.
*   **Arguments:**
    *   `path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., 'tmp://agent.card.json'). Optional path to the A2A card JSON file to validate.
    *   `card_data` *(string, optional)*: Optional raw JSON string of the A2A card to validate directly.

### `a2a_generate_card_with_agent` (Family: `a2a`)
*   **Description:** Spawns the `a2acardgenerator` subagent to convert a human-written subagent `.md` definition file into a formal, validated A2A Agent Card JSON file.
*   **Arguments:**
    *   `path` *(string)*: Relative path under the project workspace or VFS URI (e.g., 'src/plugins/securityagent/securityagent.md') of the agent .md definition file.
    *   `output_path` *(string, optional)*: Relative path under the project workspace or VFS URI (e.g., 'src/plugins/securityagent/securityagent.card.json'). Optional output card path (defaults to sidecar next to .md).
