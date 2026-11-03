# Address Lookup (`address_lookup`)

## Overview & Goals
`turbostar::address_lookup` (`src/address_lookup.h`, `src/address_lookup.cpp`) translates instruction pointer addresses and ELF symbol virtual addresses into human-readable function names, project-relative source file paths, and line/column numbers. It serves both high-frequency CPU profiling (`perf_manager`), crash dump stack trace symbolication (`crash_process`), and ELF symbol table inspection (`elf_list_symbols`).

## Architecture & Constraints
- **Batch Resolution (`resolve_addresses`)**: Deduplicates input addresses and invokes `addr2line` or `eu-addr2line` in a single batch subprocess call to avoid per-address process fork overhead.
- **Dual Target Modes**:
  - **Process Memory Maps (`/proc/<pid>/maps`)**: Parses executable VMA mappings and uses `eu-addr2line -M <maps>` or grouped per-binary `addr2line -e <elf>` calls with relative file offsets.
  - **Direct ELF Binary Paths**: Detects when `maps_path_or_pid` refers directly to an ELF binary (no VMA map entries) and invokes `addr2line -a -f -C -e <elf>` directly on the target file.
- **No Shell Execution**: Executes `addr2line` / `eu-addr2line` via `fork()` and `execvp()` without `/bin/sh` to prevent shell injection from file paths.

## Lessons Learned
- **Direct ELF Binary vs Memory Map File Disambiguation**: `eu-addr2line -M <path>` expects a `/proc/<pid>/maps` text file and fails if passed an ELF binary directly. Checking `parse_maps(maps_path).empty()` first ensures direct ELF binary lookups always use `-e <elf_path>`.
