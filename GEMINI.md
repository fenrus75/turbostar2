# TurboStar editor

Top design documentation: `docs/design.md`


# Project specific rules

- keep `docs/design.md` and related documents updated at all times. Update the specific documentation files listed in the "Documentation Files" section whenever architectural or structural changes occur.
- when adding a new `.cpp` source file, you MUST update `meson.build` and `src/meson.build`. A common mistake is to add it to the main `turbostar` target but forget to add it to the `agentcli_sources` list or relevant unit test targets.
- git commit after each logical change or item implemented. This is a standing rule.
- when adding a new `event_type` enum value, you MUST update the central `editor::dispatch` function in `src/editor_events.cpp` to route the new event to its appropriate handler; missing this is a common source of bugs.
- perform a code review before each commit to ensure no stray edits happened
- run the test suite before commit
- when fixing a bug, create a testcase BEFORE fixing the bug; the testcase
    should first fail, and pass once the bug is fixed.
- when splitting a large source file into multiple files, always add a block comment at the top of the original file describing the new files and their general contents to aid discoverability.
- read `.clang-format` on startup

## Documentation Files
The `docs/` directory contains crucial context. Keep these files updated as we make changes to the system:

| Filename | Short Description |
|---|---|
| `button-recipe.md` | Guide for implementing Turbo Pascal style UI buttons. |
| `colorscheme.md` | Defines the Turbo Pascal 7 color palette and ncurses pairs. |
| `design.md` | Top-level architectural and design documentation. |
| `file-dialog.md` | Specification for the Turbo Pascal style file dialog. |
| `general-c++.md` | C++20 coding guidelines and rules. |
| `joe-keys.md` | Reference for the "joe" dialect Wordstar keybindings. |
| `keybindings.md` | Complete list of implemented keyboard shortcuts. |
| `release-checklist.md` | Step-by-step checklist for releases and RCs. |
| `sandbox.md` | Details the systemd-based sandboxing and security strategy. |
| `style.md` | C++ coding style guide and formatting conventions. |
| `test-guidelines.md` | Guidelines and best practices for the E2E testing framework. |
| `testcoverage.md` | Guide on generating and reading test coverage reports. |
| `todo.md` | Short-term task backlog and long-term completed items tracker. |
| `tools.md` | Comprehensive schema and registry of all LLM agent tools. |

## `docs/todo.md` specific rules
- This file is frequently edited by the human.
   - Re-read the file before working on TODO items or evaluating what to
     work on next.
   - Re-read the file after completion of a TODO item.
- Move completed items to the Done section.
   - Re-read the todo.md file before doing the edit!
- Add any items deferred during other activities to the short-term section.

# Dependencies
- CLI11 (header-only) for command-line parsing.


tem.
- Move completed items to the Done section.
   - Re-read the todo.md file before doing the edit!
- Add any items deferred during other activities to the short-term section.

# Dependencies
- CLI11 (header-only) for command-line parsing.

