# Skill Manager (`skill_manager`)

## Goals
The `skill_manager` class manages the discovery, registration, mounting, and activation of modular domain skills in Turbostar. Skills are folders of specialized instructions, YAML frontmatter, references, and scripts (`SKILL.md`) that teach agents specialized tasks (e.g., performance profiling, security audits, git workflows) on demand.

## Architecture and Constraints
- **Filesystem & Dynamic Discovery**:
  - Automatically scans user and project skill directories (`~/.copilot/skills/`, `.turbostar/skills/`) during `initialize()`.
  - Mounts discovered skill resources into the `skills://` virtual file system namespace.
  - Supports dynamic programmatic registration from plugins via `register_skill(...)`.
- **Visibility & Context Injection**:
  - Tracks skill visibility (`visible` flag) to determine which skills are displayed in UI menus and auto-advertised to agents.
  - Formats skill contents into XML `<skill_content>` blocks for dynamic injection into conversation prompts when an agent activates a skill.
- **Concurrency**:
  - Mutex `mutex_` protects the internal `skills_` vector and VFS mounting operations during runtime updates.

## Lessons Learned
- **Virtual File System Mounting**: Mounting skills into `skills://` rather than reading raw filesystem paths allows subagents to browse skill helper scripts, examples, and reference docs using standard read tools (`fs_read_lines`) within sandbox boundaries.
- **Lazy Content Injection**: Advertising available skills by title and description alone, then injecting full content only when an agent invokes `activate_skill`, saves thousands of prompt tokens on inactive domains.
