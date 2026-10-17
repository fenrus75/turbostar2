# System Virtual File System Provider (`system_vfs_provider`)

## Goals
The `system_vfs_provider` class (`src/vfs/system_vfs_provider.h`, `src/vfs/system_vfs_provider.cpp`) implements the `system://` Virtual File System scheme in Turbostar.
It provides:
1. **Embedded Static Documents**: Serves reference manuals, architecture docs, guidelines, and language standards (e.g. `system://languages/cpp23.md`, `system://docs/design.md`) embedded directly in the application binary via `system_docs_embedded.h`.
2. **Dynamic Runtime Generators**: Generates real-time markdown files reflecting active application state:
   - `system://tools.md` & `system://tools_detailed.md`: Live directory and schemas of registered agent tools, supporting `?search=<term>` filtering.
   - `system://agents.md` & `system://subagents/`: Active subagents, transcripts, and final results.
   - `system://tool_families.md`: Active and available tool families.
   - `system://project/testlist.md`: Discovered test targets.
   - `system://mcp.md`: Configured and active MCP servers.
3. **Directory Listing & Metadata**: Supports `list_directory`, `exists`, and `get_file_info` over both static and dynamic resources.

## Architecture and Constraints
- **Thread Safety**: Access to `generators_` and `descriptions_` is protected by `generators_mutex_`.
- **Search Filtering**: Query filtering (`?search=...`) uses `utf8::to_lower` to ensure case-insensitive matching without locale-dependent behavior or signed char undefined behavior.

## Lessons Learned
- **Case-Insensitive Searching**: Using `utf8::to_lower` ensures query filtering across registered tools and document paths remains deterministic, locale-independent, and safe against signed-char bugs.
