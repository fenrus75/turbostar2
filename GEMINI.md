# TurboStar editor

Top design documentation: `docs/design.md`


# Project specific rules

- keep `docs/design.md` and related documents updated at all times. Update the specific documentation files listed in the "Documentation Files" section whenever architectural or structural changes occur.
- when adding a new `.cpp` source file, you MUST update `meson.build` and `src/meson.build`. A common mistake is to add it to the main `turbostar` target but forget to add it to the `agentcli_sources` list or relevant unit test targets.
- git commit after each logical change or item implemented. This is a standing rule.
- **CRITICAL**: When adding a new `event_type` enum value (in `src/event_queue.h`), you **MUST** update the central routing switch statement in `editor::dispatch` inside `src/editor_events.cpp` to route the new event to its handler. Since `editor::dispatch` has a `default: break;` case, a missing mapping compiles with NO warnings but silently discards the event at runtime.
- **CRITICAL**: Every unit test MUST isolate the `HOME` environment variable to a temporary directory to prevent race conditions during parallel test execution and avoid overwriting user configuration files in `~/.cache/turbostar/`. This is handled automatically when calling `test_watchdog::setup_watchdog()` at the start of `main()`. For custom isolated paths, use `test_watchdog::scoped_test_home guard("custom_prefix");` or `test_watchdog::isolate_home("custom_prefix");`.
- perform a code review before each commit to ensure no stray edits happened
- when working on a crash report, use the process from `docs/turbostar-crash-analysis-protocol.md`
- run the test suite before commit
- when fixing a bug (not: new feature), create a testcase BEFORE fixing the bug; the testcase
    should first fail, and pass once the bug is fixed.
- when splitting a large source file into multiple files, always add a block comment at the top of the original file describing the new files and their general contents to aid discoverability.
- read `.clang-format` on startup
- prefer std::format over string concatenations, and clean up any existing cases as you see them
- prefer `std::string_view` for read-only string function parameters over `const std::string &`, and `std::span<const T>` for read-only contiguous sequence parameters over `const std::vector<T> &` to avoid unnecessary allocations; use `std::string` / `std::vector` when ownership or mutation is required; avoid raw `char *` except when interfacing with C APIs or kernel calls
- the project uses C++23
- follow RAII principles strictly; avoid raw `new` and `delete`, favoring `std::make_unique` and `std::make_shared`
- strongly prefer C++ Standard Library containers and algorithms over custom implementations
- label methods and parameters `constexpr`, `const`, `std::string_view`, `std::span`, and `noexcept` whenever appropriate
- each class in a separate .cpp file with a dedicated .h file that is in the same directory as the .cpp file
- add extensive comments describing goals, constraints, ownership, and design intent (the "why"), rather than restating code implementation logic.
- **Security & Untrusted Data Lifecycle**:
  - Adopt a security-first mindset when generating code.
  - **Definition**: Any data originating from user input, CLI flags, network requests (HTTP/A2A), LLM prompts, subagent outputs, environment variables, database records, or un-canonicalized file paths MUST be treated as **untrusted**.
  - **Explicit Variable & Parameter Naming**:
    - **Variable Names**: All variables holding untrusted data MUST use the `untrusted_` prefix (e.g., `untrusted_path`, `untrusted_url`, `untrusted_json`).
    - **Function Parameters**: Parameters taking untrusted data MUST be named with the `untrusted_` prefix OR annotated with `/* untrusted */` in the signature (e.g., `void parse_card(/* untrusted */ const std::string &untrusted_json)`).
  - **Data Cleaning & Approved Conversion Functions**:
    - Never operate directly on `untrusted_` variables. Clean or validate them using approved boundary functions, and assign the output to a clean variable name (e.g., `safe_path`, `canonical_url`, `clean_name`):
      1. **Path & File Security**:
         - `ctx.fs_security.validate_access(untrusted_path, access_type, OUT canonical_path, OUT error)`: Resolves and checks sandbox permissions.
         - `fs_utils::is_regular_file(untrusted_path)`: Verifies path is a regular file (blocks FIFOs, sockets, and device nodes).
         - `fs_utils::is_valid_db_name(untrusted_name)`: Enforces `[a-zA-Z0-9_-]` whitelist for SQLite DB names.
      2. **Shell Injection Prevention**:
         - `fs_utils::escape_shell_arg(untrusted_str)`: Escapes arguments safely for shell execution.
         - `fs_utils::is_shell_safe(untrusted_str)`: Checks allowlist of safe shell characters.
         - `fs_utils::format_command(...)`: Formats shell commands while automatically escaping all string/path arguments.
      3. **Prompt & Context Injection Prevention**:
         - `fs_utils::wrap_prompt_untrusted_data_tag(tag, untrusted_content)`: Wraps data in XML tags (`<tag>...</tag>`) and neutralizes closing tag breakout attempts (`</tag>`).
         - `fs_utils::escape_json_string(untrusted_content)`: Escapes control bytes and quotes for JSON payloads.
      4. **UI & Control Character Sanitization**:
         - `fs_utils::is_safe_for_ui(untrusted_str)`: Rejects non-printable characters and ANSI escape sequences.
      5. **SQL Safety**:
         - `sqlite_query_validation::strip_comments_and_check_restricted_keywords(untrusted_sql)`: Strips comment blocks before checking restricted SQL commands.
  - Verify all buffer and array accesses against underflows and overflows.
- all `#include ""` should be relative to the `src/` directory (e.g., `#include "fs_utils.h"` or `#include "agentlib/tool_registry.h"` instead of using relative `../../` paths), since `src/` is in the compiler include paths.
- use #pragma ONCE for include guards
- when declaring a mutex in a header file, you MUST add a comment block immediately preceding the declaration explaining: (1) what specific member data or resources the mutex protects, and (2) the general locking rules, lifecycle, or ordering guidelines associated with it.
- when creating a subclass, add or update the header of the parent class with a table that matches this example template:
```c
/*

# subclasses of <parent class>

| subclass     | filename                                             |
| ------------ | ---------------------------------------------------- | 
| <subclass 1> | <project relative path to the header for subclass 1> |

*/
```
	Check is such comment with table already exists, and add a line when it does.
	When no such comment exists, create a new comment ABOVE the class definition.


## Documentation Files
The `docs/` directory contains crucial context. Keep these files updated as we make changes to the system:

| Filename | Short Description |
|---|---|
| `button-recipe.md` | Guide for implementing Turbo Pascal style UI buttons. |
| `colorscheme.md` | Defines the Turbo Pascal 7 color palette and ncurses pairs. |
| `design.md` | Top-level architectural and design documentation. |
| `design-perf-integration.md` | Architecture for CPU cycle profiling using perf_event_open and libturbocatch.so. |
| `design-plugin.md` | Plugin architecture, lifecycles, and tool registration. |
| `design-pure.md` | Tool purity, state domain classification, and read-only agent security rules. |
| `file-dialog.md` | Specification for the Turbo Pascal style file dialog. |
| `general-c++.md` | C++20 coding guidelines and rules. |
| `joe-keys.md` | Reference for the "joe" dialect Wordstar keybindings. |
| `keybindings.md` | Complete list of implemented keyboard shortcuts. |
| `release-checklist.md` | Step-by-step checklist for releases and RCs. |
| `sandbox.md` | Details the systemd-based sandboxing and security strategy. |
| `style.md` | C++ coding style guide and formatting conventions. |
| `test-guidelines.md` | Guidelines and best practices for the E2E testing framework. |
| `testcoverage.md` | Guide on generating and reading test coverage reports. |
| `thread-lifecycle.md` | Blueprint for thread management and subprocess teardown. |
| `todo.md` | Short-term task backlog and long-term completed items tracker. |
| `tools.md` | Comprehensive schema and registry of all LLM agent tools. |

## `docs/todo.md` specific rules
- This file is frequently edited by the human.
   - Re-read the file before working on TODO items or evaluating what to
     work on next.
   - Re-read the file after completion of a TODO item.
   - NEVER git checkout / git reset this file
- Move completed items to the Done section.
   - Re-read the todo.md file before doing the edit!
- Add any items deferred during other activities to the short-term section.

# Dependencies
- CLI11 (header-only) for command-line parsing.

# Tooling
- `agentcli` (along with `agentcli_record` and `agentcli_replay` executables) is available to record and replay conversations with the LLM. It is used by the test suite to verify tool execution and agent logic in headless environments without requiring a live network connection to the LLM.