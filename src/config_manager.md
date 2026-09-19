# Configuration Manager (`config_manager`)

## Goals
The `config_manager` class serves as the central configuration and settings authority for Turbostar. It provides two-tiered configuration persistence:
1. **Global User Settings**: Stored in `~/.turbostar/config.ini` for cross-project preferences (e.g., default AI models, tab widths, editor styling, global tool families, A2A server ports).
2. **Project-Local Overrides**: Stored in `<project_root>/.turbostar/config.ini` to allow individual repositories to declare custom build directories, preferred build systems, model mappings, and project-scoped tool family authorizations without polluting global settings.

It also acts as the central query interface for application-wide runtime execution modes including YOLO mode (`is_yolo_mode`), paranoid security sandboxing (`is_paranoid_mode`), compiler auto-save rules, and MCP server activation criteria.

## Architecture and Constraints
- **Singleton Lifecycle**: Instantiated via `config_manager::get_instance()`. Loaded early in `main()` before subsystem initialization.
- **Hierarchical Loading & Merging**: Global settings are loaded first, followed by project-level overrides. When saving project configurations, only settings explicitly overridden or designated as project-local are serialized.
- **INI Serialization**: Serializes configuration key-value pairs using standard INI section syntax (`[section] key=value`) to allow inspection and editing using standard external text utilities.
- **Thread Safety**: 
  - Hot runtime flags that are toggled interactively from UI menus or slash commands while worker threads execute (such as `yolo_mode_`) use atomic primitives (`std::atomic<bool>`).
  - Complex maps (such as tool family authorizations and task model aliases) are populated at startup and read-only during normal turn processing or synchronized through component lifecycles.

## Lessons Learned
- **Distinguishing User Intent from Auto-Detection**: Build system detection (`auto_detect_build_system`) must track `is_build_system_explicit_`. If a user manually configures CMake over Meson, subsequent loads must respect the user's explicit choice rather than overwriting it with heuristic filesystem detection.
- **Lock-Free YOLO Mode**: Worker threads and background agent tool validators frequently inspect `is_yolo_mode()` to decide whether to prompt the user. Storing `yolo_mode_` as `std::atomic<bool>` eliminates locking overhead and avoids deadlocks during UI prompt promise dispatch.
- **Sandboxing Cache Paths**: Build tools like `ccache` and `meson` rely on persistent user cache paths. Configuration settings like `allow_code_execution_network` and `run_outside_sandbox` must be validated against security policies before applying exceptions to `command_runner` profiles.
