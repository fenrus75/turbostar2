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
*   **Description:** Reads a specific range of text lines from a file. Output lines are prefixed with their 1-based line number in `"<line_number>: <line_text>"` format.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file, relative to the project root.
    *   `start_line` *(integer, optional)*: The 1-based line number to start reading from. Defaults to 1 if omitted.
    *   `end_line` *(integer, optional)*: The 1-based line number to end reading at (inclusive). Defaults to reading the rest of the file if omitted.

### `fs_grep_files`
*   **Description:** Search for a pattern (string or RE2 regular expression) across multiple files in the project. Use this instead of grep. Returns formatted markdown with line numbers and matches. Ideal for finding definitions, usages, or error messages across the codebase.
*   **Arguments:**
    *   `pattern` *(string, required)*: The RE2 regular expression to search for.
    *   `include_ext` *(string, optional)*: Filter by file extension (e.g., '.cpp', '.py').
    *   `dir_path` *(string, optional)*: Restrict search to a specific directory path relative to project root. Defaults to the document root if omitted.
    *   `max_results` *(integer, optional)*: Cap the total number of detailed matches to prevent blowing out the context window. Defaults to 50. If exceeded, only filenames are listed for the remaining matches.

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

### `fs_list_tests`
*   **Description:** Returns a markdown table of available test names in the project, optionally filtered by a pattern.
*   **Arguments:**
    *   `pattern` *(string, optional)*: Optional pattern (string or RE2 regular expression) to filter test names.

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
        *   `original_text` *(string, optional)*: Required for `remove` and `replace`. The exact full content of the original line(s) being modified. You MAY provide multiple lines separated by \n to replace entire blocks of code. Used for safety verification. Pass empty string for `add`.
        *   `replace_with` *(string, optional)*: Required for `add` and `replace`. The new content to insert or replace the line with. You MAY use newline characters (\n) here to insert multiple lines. Pass empty string for `remove`.

### `fs_write_file`
*   **Description:** Creates a new file, overwrites an existing file, or safely appends content to an existing file.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file to write, relative to the project root.
    *   `content` *(string, required)*: The entire complete content to write into the file, or content to append.
    *   `force_overwrite` *(boolean, optional)*: Set to true to overwrite an existing file. Defaults to false. Mutually exclusive with `append`.
    *   `append` *(boolean, optional)*: Set to true to safely append `content` to the end of an existing file. Defaults to false. Mutually exclusive with `force_overwrite`.

### `fs_mkdir`
*   **Description:** Create a directory, including any necessary parent directories (like mkdir -p).
*   **Arguments:**
    *   `path` *(string, required)*: The path to the directory to create, relative to the project root.

---

## 3. Compilation & Diagnostics

### `fs_run_tests`
*   **Description:** Runs the project's test suite (synchronously) and returns the console output. Catch crashes and dumps backtraces. Runs with terminal interaction.
*   **Arguments:** None.

### `fs_compile_project`
*   **Description:** Compiles the entire project and returns the raw console output. Populates the workspace error list. Can be run asynchronously.
*   **Arguments:**
    *   `async` *(boolean, optional)*: If true, runs the compilation asynchronously in the background. Defaults to false.

### `fs_compile_file`
*   **Description:** Compiles a single file and returns the raw console output. Populates the workspace error list. Can be run asynchronously.
*   **Arguments:**
    *   `path` *(string, required)*: The path to the file to compile, relative to the project root.
    *   `async` *(boolean, optional)*: If true, runs the compilation asynchronously in the background. Defaults to false.

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

### `open_in_editor`
*   **Description:** Open a file in the editor UI for the user to view or edit. If the file is already open in a window, that window is activated and focused; otherwise, the file is loaded in a new window.
*   **Arguments:**
    *   `filename` *(string, required)*: The path of the file to open in the editor.

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

### `web_fetch`
*   **Description:** Fetches content from a URL via HTTP/HTTPS. Useful for reading documentation or external resources. Implements domain-based access controls and prompts the user for permission.
*   **Arguments:**
    *   `url` *(string, required)*: The full URL to fetch (must start with http:// or https://).
    *   `no_ask` *(boolean, optional)*: If true, the tool will fail silently with a permission error if the domain is not pre-approved, rather than prompting the user for permission.

### `run_shell_command`
*   **Description:** Runs an arbitrary shell command safely within the sandbox. The command will be subject to user permission approval.
*   **Arguments:**
    *   `command` *(string, required)*: The exact shell command to execute.

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

### `agent_mark_milestone`
*   **Description:** Used to signal that a major task is complete or that you are pivoting to a completely new area. This helps the system manage long-term memory and context windows efficiently by compressing old history.
*   **Arguments:**
    *   `title` *(string, required)*: A short title for the completed task or the new milestone.
    *   `summary` *(string, required)*: A concise summary of the work that was just completed and the goal of the new phase.

### `agent_compress_history`
*   **Description:** Proactively pages out conversational history prior to this tool call into a saved milestone archive. This frees up your context window. A highly dense pointer message replaces the old history, allowing you to restore it later if needed.
*   **Arguments:**
    *   `title` *(string, required)*: A short title for the milestone you are archiving.
    *   `summary` *(string, required)*: A concise summary of the history being paged out.
    *   `tags` *(array of strings, optional)*: Semantic tags to label this archive.
    *   `target_episode_id` *(string, optional)*: The exact ID of the milestone or system message (e.g., 'episode_123') that acts as the UPPER boundary. If omitted, pages out the active/current block.
    *   `include_all_prior` *(boolean, optional)*: If true, ignores the lower boundary and compresses everything from the target back to the system prompt.

### `agent_restore_context`
*   **Description:** Pages in a previously saved context archive (episode). Use this if you need to resume work on an old task or look up historical context. Find the episode_id by using the '/memory' command or reading the SYSTEM MEMORY pointers in your history.
*   **Arguments:**
    *   `episode_id` *(string, required)*: The exact ID of the episode to restore.
    *   `compression_level` *(integer, optional)*: Controls how aggressively the archive is optimized during restoration. `0` = Raw history. `1` = Think-Free reasoning stripping (default). `2` = Terminal truncation / active level 2. Defaults to `1`.

### `agent_list_episodes`
*   **Description:** Lists all archived/paged-out episodes, returning a markdown table showing the Episode ID and their 'when to resume' reactivation hint.
*   **Arguments:** None.

### `pop_todo`
*   **Description:** Removes and returns the first item from the agent's todo list. Useful for treating the todo list as a sequential task queue.
*   **Arguments:** None.

### `agent_set_timer`
*   **Description:** Sets a timer (in seconds) that runs in the background. Once the timer expires, if the agent is idle, it injects a `"previously set timer expired"` system message to wake the agent.
*   **Arguments:**
    *   `seconds` *(integer, required)*: The duration of the timer in seconds.

---

## 8. Subagent Orchestration

### `create_agent`
*   **Description:** Creates a new subagent to delegate tasks to. You must provide either a `task` (user request) or a `profile` (system instructions), or both.
*   **Arguments:**
    *   `name` *(string, required)*: A short, descriptive name for the subagent.
    *   `profile` *(string, optional)*: System instructions and personality profile for the subagent. Required if `task` is omitted.
    *   `task` *(string, optional)*: The initial task or request for the subagent to perform. Required if `profile` is omitted.
    *   `wait` *(boolean, optional)*: If true, the tool will wait for the subagent to complete its task and will return its final response directly. Defaults to false (asynchronous).

### `list_agents`
*   **Description:** Lists all active subagents managed by the current agent. Returns a Markdown table of ID, Name, and Status.
*   **Arguments:** None.

### `agent_status`
*   **Description:** Returns detailed status information about a specific subagent.
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent to query.

### `message_agent`
*   **Description:** Sends a message or command to an active subagent.
*   **Security:**
    *   Maximum message length: 100KB (prevents DoS attacks)
    *   Validates target agent exists and is a direct child of the calling agent
*   **Arguments:**
    *   `id` *(integer, required)*: The ID of the subagent.
    *   `message` *(string, required)*: The text message or instruction to send (max 100KB).

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
*   **Description:** View the last 10 commit messages in the repository (git log -n 10 --oneline).
*   **Arguments:** None.

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
*   **Description:** Starts the main application executable, optionally under GDB debugging with split screen. Returns JSON with `app_run_id` and `gdb_run_id`.
*   **Arguments:**
    *   `args` *(string, optional)*: Command line arguments to pass to the application.
    *   `debugger` *(boolean, optional)*: If true, starts the application with a split screen debugger (GDB/GDBServer). Defaults to false.

### `agent_write_to_run`
*   **Description:** Writes/injects keyboard input sequences into the application or debugger PTY master stream.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.
    *   `data` *(string, required)*: The raw string data or escape sequence to inject.

### `agent_get_run_screenshot`
*   **Description:** Returns a snapshot/screenshot of the terminal buffer grid, cursor coordinates, and visibility status for a given run ID.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.

### `agent_terminate_run`
*   **Description:** Terminates/stops a running process and closes its window based on its run ID.
*   **Arguments:**
    *   `run_id` *(integer, required)*: The unique execution ID returned by `agent_start_app`.