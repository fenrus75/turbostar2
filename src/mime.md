# MIME Detection and Language Utilities (`mime`)

## Goals
The `mime` namespace (`src/mime.h`, `src/mime.cpp`) provides MIME type sniffing, human-readable file type descriptions, syntax highlighting language identification, and brace syntax detection for editor buffers and files.

## Architecture and Constraints
- **File and Buffer MIME Sniffing (`detect_file_type`, `detect_buffer_type`)**:
  - Direct integration with `libmagic` via `magic_open(MAGIC_MIME_TYPE)` when available (`HAS_LIBMAGIC`).
  - Fallback cascades:
    - `detect_buffer_type`: inspects magic signatures for common image formats (PNG, JPEG, GIF) before defaulting to `"application/octet-stream"`.
    - `detect_file_type`: checks file existence, runs `magic_file`, falls back to inspecting the first 8 bytes via `detect_buffer_type`, and finally falls back to extension mapping via `from_extension`. Bypasses local filesystem checks for URI schemes (`file://`, `http://`, etc.).
- **Human-Readable Descriptions (`detect_file_description`, `detect_buffer_description`)**:
  - Uses `magic_open(MAGIC_NONE)` to obtain descriptive file strings (e.g. `"PDF document, version 1.4"`, `"C source, ASCII text"`).
- **Extension & Language Mapping (`from_extension`, `get_language_from_extension`, `uses_brace_syntax`)**:
  - Normalizes path extensions and bare extensions case-insensitively.
  - Maps common source file extensions to canonical markdown / highlight language identifiers (e.g., `.cpp`/`.cc` -> `"cpp"`, `.py` -> `"python"`, `.cs` -> `"csharp"`).
  - Determines whether an extension or file syntax is brace-delimited (`uses_brace_syntax`), enabling smart brace matching and indentation rules.

## Lessons Learned
- **Bare Extension Handling in `uses_brace_syntax`**: `std::filesystem::path("cs").extension()` returns an empty string when the path contains no dot. Fallback logic must check if the string contains no directory separator, strip any leading dot, and match directly against known extension lists so that `"cs"`, `".cs"`, and `"path/to/file.cs"` resolve identically.
- **Embedded Null Bytes in Binary String Literals**: When testing binary headers in C++, initializing `std::string` or `std::string_view` from a raw C-string literal (`"..."`) triggers `strlen()`, which truncates at the first embedded `\0` byte. Tests must use `using namespace std::string_literals; "..."s` or `using namespace std::string_view_literals; "..."sv` to preserve binary payloads.
- **Test Build Dependency Isolation (`magic_dep`)**: When adding unit test executables for modules relying on optional libraries (`libmagic`), the test target in `tests/meson.build` must explicitly include `magic_dep` in its `dependencies` list. Otherwise, `HAS_LIBMAGIC` remains undefined during test compilation even though the main library target has it enabled.
- **RAII Resource Management for `magic_t`**: Raw calls to `magic_open` and `magic_close` create duplicated cleanup code and error-prone leak hazards across multiple early returns. Wrapping `magic_t` in a `std::unique_ptr<struct magic_set, magic_cleanup>` RAII type ensures handles are unconditionally closed on scope exit.
- **Fast-Path Binary Image Signatures**: Truncated 4-byte buffers (such as `\x89PNG` or `\xff\xd8`) are classified by `libmagic` as `text/plain` or `application/octet-stream` rather than image types because libmagic expects complete container headers. Checking the 4-byte magic signature for PNG, JPEG, and GIF directly before invoking libmagic provides a reliable fast path and enables accurate identification of partial or truncated assets.
- **Coverage Accuracy and Unit Test Library Linking**: Recompiling a library source file directly into a unit test executable (`tests/meson.build`) rather than linking against `core_dep` causes gcov profiling data to split into separate object files (`tests/test_mime.p/.._src_mime.cpp.o` vs `src/libturbostar_core.a.p/mime.cpp.o`), giving the illusion of missing coverage on the core library. Linking unit tests with `core_dep` unifies coverage tracking across the codebase.

