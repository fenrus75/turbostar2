# TurboStar Release Notes

## v0.1.0 (Upcoming Initial Release)

TurboStar is a modern terminal-based (TUI) text editor and autonomous AI agent environment, combining the classic Turbo Pascal 7 visual design and WordStar ("joe" dialect) keybindings with native C++23 performance, Language Server Protocol (LSP) integration, and deep LLM agentic capabilities.

### Highlights

#### 1. Turbo Pascal TUI Editor
* **Classic Aesthetics**: 16-color retro Turbo Pascal 7 color palette, double-line framed windows, drop shadows, and menu bar.
* **WordStar / Joe Keybindings**: Stateful block selection (`^KB`, `^KK`), file operations (`^KO`, `^KS`), window management, and rapid navigation.
* **LSP Integration**: Automatic background discovery and lifecycle management for `clangd` (C/C++), `pylsp`/`pyright` (Python), providing real-time diagnostics, hover info, jump-to-definition, and semantic tokens.
* **Git Integration**: Dirty-state indicator, branch detection in window frames, and integrated compile/test output split views.

#### 2. Built-in AI Agentic Environment
* **Interactive Agent Workspace**: Dedicated agent pane (`^KA`) capable of inspecting codebases, executing terminal tools in sandboxed environments, and applying surgical file diffs.
* **Virtual Paged AI Context**: Intelligent virtual memory paging system for LLM context windows to eliminate context blowout during long-running sessions.
* **Atomic Undo/Redo**: All agent-directed file modifications integrate with editor document undo history for single-keystroke reviews and reversions.
* **Automated Crash Analysis**: Out-of-process crash dump analysis (`turbostar-crashprocess`) with libunwind backtraces and automated reproduction protocols.

#### 3. Protocols & Tool Architecture
* **Model Context Protocol (MCP)**: Native stdio server mode (`turbomcp`) exposing all TurboStar editing, inspection, and analysis tools to external agents.
* **Agent-to-Agent (A2A)**: Headless distributed agent server (`turboserver`) with bearer token authentication and git worktree isolation.
* **Extensible Dynamic Plugins**: Modular shared library (`.so`) tool plugins for hex editing, binary analysis, security scanning, image inspection, and SQLite.
