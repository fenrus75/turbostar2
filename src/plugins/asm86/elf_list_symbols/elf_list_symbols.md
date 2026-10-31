# elf_list_symbols Tool

## Overview
The `elf_list_symbols` tool inspects an ELF binary file and extracts symbols from its symbol table (`.symtab` or `.dynsym`), returning their name, offset/value, and size. It supports optional pattern filtering using PCRE2 regular expressions or substring matches.

## Constraints & Security
- Untrusted inputs (`path`, `pattern`) are validated through standard tool validators.
- Paths are validated with `fs_security.validate_access` to enforce project root containment and read permissions.
- Regular expressions are validated at parse time and executed using `pcre2_regex` with backtracking and match limits to prevent ReDoS attacks.
- Output symbol count is capped at 200 to protect context window size.
- Markdown table output characters (e.g. `|`, control codes) are sanitized to prevent Markdown table formatting breakout.

## Lessons Learned
- Migrated from RE2 to PCRE2 (`pcre2_regex`) to eliminate Abseil and RE2 dependencies from binary plugins and core executables.
