# C/C++ Execution Tool (`run_cpp`)

## Goals
The `run_cpp` tool compiles and executes standalone C or C++ source code snippets and translation units in an isolated, sandboxed environment:
1. Provide fast, permissionless verification of C/C++ language features, preprocessor macros, standard library types, and external APIs.
2. Intercept and format crash dumps via `libturbocatch.so` LD_PRELOAD instrumentation when snippets fault (SIGSEGV, SIGABRT, assertions).
3. Support seamless interaction with Turbostar's Virtual Filesystem (VFS) including `include://` (transparent access to system headers) and `tmp://` (virtual scratch files and headers).
4. Provide standard GCC/Clang ergonomics via `cflags` and `ldflags` with proper ELF link ordering.

## Architecture and Constraints
- **Compilation vs Execution**: Spawns GCC/Clang under `sync_command_runner` with strict timeouts and project working directory context.
- **Language Standard Auto-Defaulting**: If `-std=` is not specified in `cflags`, automatically falls back to `-std=c17` for `.c` source files or the project's configured language standard from `config_manager::get_instance().get_primary_language_version()` (e.g. `-std=c++23`).
- **Direct VFS Directory Resolution**: Translates `-Iinclude://` and `-Itmp://` to local directory paths directly without redundant file copying whenever local directories exist on the host filesystem.
- **Linker Flag Ordering**: In ELF linking, `-l<lib>` and `-L<path>` flags must appear *after* object and source files; `ldflags` are placed strictly at the end of the compiler invocation command line.
- **pkg-config Dependency Resolution**: `packages` (aliases: `pkg_config`, `pkgconfig`, `pkgs`) automatically queries `pkg-config --cflags --libs <packages...>`, routing `-I` and `-D` flags into `cflags` and `-l`, `-L`, `-Wl,` flags into `ldflags`. Package names are validated against `fs_utils::is_shell_safe()`.

## Lessons Learned
- **Replacing Wrapper Parameters with Standard Flags**: Dedicated wrapper parameters (`defines`, `includes`, `libraries`, `std`) created conceptual bloat and confusion (such as `libraries` doubling as both `-l...` linker flags and `.cpp` extra translation units). Consolidating into `cflags`, `ldflags`, `packages`, and `sources` gives callers complete flexibility (optimizations `-O2`, warnings `-Wall`, sanitizers `-fsanitize=...`) while matching standard build system semantics.
- **Project Language Standard Inheritance**: Hardcoding `-std=c++23` prevented projects with different standards from testing accurately; inheriting `get_primary_language_version()` allows `run_cpp` to adapt naturally to whatever C++ standard the active project configures.
- **Automated Pkg-Config Integration**: Specifying system library flags manually frequently led to missing transitive dependencies (e.g. `libpng` requiring `-lz -lm`). Adding `packages` delegates flag resolution to `pkg-config`, reducing cognitive load on callers.
