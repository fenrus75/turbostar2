# fs_basics - Core Pure Primitives & String Utilities

## Overview & Goals
`fs_basics` (`src/fs_basics.h`, `src/fs_basics.cpp`) contains foundational, standalone utility functions and algorithms that have **zero dependencies** on TurboStar subsystems (such as `project_manager`, `event_logger`, `command_runner`, or `build_error_manager`). 

It was extracted from `src/fs_utils.cpp` to decouple pure algorithmic primitives from complex, project-aware filesystem operations, making these essential routines lightweight, fast to compile, and easy to unit-test in isolation.

## Capabilities
- **Base64 Encoding & Decoding**: Standard RFC 4648 Base64 encoding/decoding supporting `std::string_view` and `std::span<const unsigned char>`, with binary round-trip fidelity.
- **Binary Buffer Detection**: Heuristic scanning of memory buffers for null bytes and non-printable control characters (`get_buffer_file_type`, `is_binary_buffer`).
- **Levenshtein Distance**: Dynamic programming edit-distance calculation between string views, used by directory suggestion and typo detection.
- **Shell Escaping & Injection Prevention**: Strict allowlist check (`is_shell_safe`), single-quote argument escaping (`escape_shell_arg`), and typesafe command formatter (`format_command`).
- **JSON Escaping & Auto-Repair**: RFC 8259 string escaping (`escape_json_string`) and auto-recovery of truncated or unclosed JSON strings/structures (`repair_json_string`).
- **Prompt Injection Defense**: Safe wrapping of untrusted data in XML-like tags (`wrap_prompt_untrusted_data_tag`) neutralizing tag breakout attempts (`</tag>` $\rightarrow$ `&lt;/tag>`).
- **UI & System Sanitization**: Non-printable and ANSI escape sequence rejection (`is_safe_for_ui`), strict database name validation (`is_valid_db_name`), filename path shortening with ellipsis (`shorten_filename`), and Linux thread naming (`set_current_thread_name`).

## Constraints & Design Principles
- **Zero Heavy Subsystem Dependencies**: Never include or depend on `project_manager.h`, `event_logger.h`, or UI libraries. All functions must operate strictly on their arguments.
- **C++ Standard Types**: Prefer `std::string_view` for input parameters, `std::span<const unsigned char>` for contiguous byte sequences, and `std::string` / `std::vector<unsigned char>` for owned return values.
- **Ctype Safety**: Any call to `<cctype>` functions (`std::isalnum`, `std::isspace`) MUST explicitly cast `char` arguments to `static_cast<unsigned char>` to prevent undefined behavior on non-ASCII or UTF-8 bytes.

## Lessons Learned
- **Decoupling from Project Subsystems**: Previously, `src/fs_utils.cpp` combined low-level string utilities (base64, Levenshtein, escaping) with project root resolution, cache directory management, and compiler execution. Any test or tool wanting basic base64 encoding had to link extensive project subsystems. Extracting `fs_basics` eliminates circular and heavy linkage requirements.
- **Header Unification via Forward Inclusion**: `src/fs_utils.h` includes `fs_basics.h` so that legacy callers of `fs_utils::base64_encode` or `fs_utils::escape_shell_arg` continue to compile without disruption.
- **100% Branch Coverage on Untrusted Data**: Achieving 100% coverage on untrusted data parsers requires testing malformed prefix/suffix truncations (e.g. unclosed tags `<tag`, empty tag names `<>`, and mismatched tags) as well as unbalanced inner structures in JSON auto-repair (`{"outer": {"inner": 1}, "unclosed": 2`).
- **Gcov and C++ Exception Landing Pads**: In GCC with exceptions enabled, returning a class object (like `std::string`) from a nested block creates an EH landing pad associated with the block's closing brace. Structuring functions with a single return path or early return on simple formats prevents synthetic unexecuted lines in coverage metrics.
