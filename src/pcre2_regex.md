# PCRE2 Regex Wrapper (`pcre2_regex`)

## Goals
The `pcre2_regex` class (`src/pcre2_regex.h`, `src/pcre2_regex.cpp`) provides a modern C++23 RAII wrapper around `libpcre2-8`:
1. **Dynamic Regular Expression Support**: Replaces `re2::RE2` for dynamic runtime queries, interactive document search/replace, and agent tools.
2. **Lean Dependency Footprint**: Links strictly against `libpcre2-8.so` (which depends solely on `libc.so.6`), eliminating all 36+ transitive Abseil (`libabsl_*`) shared libraries from Turbostar binaries to dramatically reduce startup latency.
3. **ReDoS Protection**: Configures safety match limits (`match_limit`) via `pcre2_match_context` to bound back-tracking and prevent catastrophic regex evaluation on untrusted inputs.
4. **PIMPL Encapsulation**: Hides all C macros, PCRE2 types (`pcre2_code`, `pcre2_match_data`), and header definitions inside `src/pcre2_regex.cpp`, ensuring clean header isolation for all consumers.
5. **JIT Acceleration**: Enables PCRE2 JIT compilation (`pcre2_jit_compile(..., PCRE2_JIT_COMPLETE)`) for high-throughput substring search and scanning.

## Architecture and Constraints
- **Thread Safety**: The compiled pattern (`pcre2_code`) is immutable and thread-safe. Per-call match data (`pcre2_match_data`) and match contexts ensure methods can be called concurrently from multiple threads.
- **UTF-8 and Unicode Properties**: Compiles with `PCRE2_UTF | PCRE2_UCP` by default, properly handling multibyte Unicode characters and character classes (`\w`, `\s`, `\b`).
- **Capture Backreferences**: Global substitution uses `PCRE2_SUBSTITUTE_EXTENDED`, supporting both standard `$1` and `\1` capture backreferences in replacement strings.

## Lessons Learned
- **PCRE2 Global Substitute Buffering**: If the output buffer passed to `pcre2_substitute` is too small, PCRE2 returns `PCRE2_ERROR_NOMEMORY` and populates `out_len` with the exact required byte capacity. Catching `PCRE2_ERROR_NOMEMORY` and re-invoking `pcre2_substitute` with the resized buffer guarantees allocation-safe string replacement without truncation.
- **Extended Replacement Sequences**: By default, `pcre2_substitute` interprets only `$1` style backreferences, leaving `\1` literally unexpanded. Passing `PCRE2_SUBSTITUTE_EXTENDED` enables Unix-style `\1` backreferences alongside `$1`.
