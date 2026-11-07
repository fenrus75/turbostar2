# `agent_get_profile_details_tool` (`src/tools/agent_get_profile_details/`)

## Overview & Goals
`agent_get_profile_details_tool` retrieves source-annotated CPU profile reports for a specific run ID (`run_id`) and optional file (`file_path`) or function (`function_name`) filter. It reads pre-symbolized `profile_report` data from `execution_manager` and formats source context around hot lines (`[42: 120 samples]` vs ` 43:       code`).

## Constraints & Performance Invariants
1. **Path Resolution Must Never Walk External or Build Trees**:
   - DWARF debug info in profiled binaries frequently contains non-project paths such as virtualized system headers (`include://bits/basic_string.h`, `include://c++/14/bits/stl_vector.h`), glibc build roots (`/build/reproducible-path/glibc-2.43/...`), and system libraries (`/usr/...`, `/lib/...`).
   - `resolve_file_path` must immediately reject URI schemes (`://`) and known external prefixes (`/usr/`, `/lib/`, `/lib64/`, `/build/`, `malloc/`) without probing the project workspace.
2. **Memoized & Indexed Fallback Resolution**:
   - A `profile_report` can contain 100+ distinct files in `line_samples_by_file` and dozens of entries in `top_functions` / `top_lines`.
   - Path resolution is memoized per tool invocation (`resolved_cache`), and filename pre-checks (`extract_filename_view`) avoid resolving non-matching candidate entries inside loops.
   - If a relative DWARF path requires searching the project root by basename, a single lazily-built `basename_index` is constructed per invocation while explicitly skipping `.git`, `build*`, `subprojects`, `node_modules`, `third_party`, `vendor`, and hidden directories via `disable_recursion_pending()`.

## Lessons Learned
- **Unbounded `recursive_directory_iterator` in Nested Profile Loops (Fixed)**:
  - Previously, querying `agent_get_profile_details` for a file not present in the global top-10 `report.top_lines` fell back to scanning `report.line_samples_by_file` and calling `resolve_file_path` inside nested loops without memoization. Every `include://...` or `/build/reproducible-path/...` entry failed direct path lookup and triggered a full `std::filesystem::recursive_directory_iterator(proj_root)` walk across `.git/` and `build/`, causing a **29–58 second** stall per call.
  - Fixing this with fast external-path rejection, filename pre-filtering, per-call memoization, and a pruned lazy basename index reduced execution time by over three orders of magnitude.
