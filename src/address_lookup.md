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
- **Debloating Demangled C++ Symbols (`utf8::clean_cxx_type_noise`)**: `addr2line -C` and `eu-addr2line -C` emit full ABI template instantiations (`std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >`, `nlohmann::json_abi_v3_11_3::basic_json<...>`, default allocators/deleters). Normalizing `function_name` through `utf8::clean_cxx_type_noise` in `parse_addr2line_output` keeps crash backtraces and CPU profiling summaries concise and readable.
