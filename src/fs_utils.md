# Filesystem & Security Utilities (`fs_utils`)

## Goals
The `fs_utils` namespace serves as the primary security boundary and filesystem abstraction for Turbostar. It enforces the **Security & Untrusted Data Lifecycle** rules, ensuring that all data originating from user input, CLI flags, network requests, or AI agents is validated, sanitized, and safely transformed before consumption by the core editor or operating system.

## Architecture and Constraints
- **Security Boundaries**:
  - `is_regular_file(untrusted_path)`: Verifies that a path points strictly to a regular file (`S_ISREG`), preventing unbounded hangs when tools attempt to read special device nodes, FIFOs, or Unix domain sockets.
  - `safe_absolute(path)`: Resolves absolute paths safely, catching filesystem exceptions and logging errors rather than terminating the process.
  - `make_relative_to_project(path)`: Normalizes absolute filesystem paths to clean, workspace-relative paths to eliminate context window token bloat for LLMs.
- **Shell Injection Defense**:
  - `escape_shell_arg(str)`: Escapes special shell metacharacters for POSIX shell execution.
  - `format_command(fmt, args...)`: Formats shell command strings while automatically escaping all string and path arguments.
- **Prompt & JSON Injection Prevention**:
  - `wrap_prompt_untrusted_data_tag(tag, content)`: Wraps untrusted strings inside XML tags (`<tag>...</tag>`) while escaping closing breakout attempts (`</tag>`).
  - `escape_json_string(content)`: Escapes control bytes and quotes for JSON payloads.
- **File Health Tracking**:
  - Centralizes single-file syntax check generation (`get_compile_command_for_file`) using `-fsyntax-only` to attribute compiler errors to specific `Edit ID #N` changes.

## Lessons Learned
- **FIFO/Device Node Hangs**: Tools attempting to open or stat arbitrary files can block permanently if given paths like `/dev/stdin` or named FIFOs. Verifying `is_regular_file` upfront eliminates mysterious background tool hangs.
- **Path Relativization in Prompts**: Presenting raw system paths (e.g. `/home/user/git/project/src/main.cpp`) consumes excessive tokens and confuses agents operating across differing host environments. Normalizing paths to project-relative roots (`src/main.cpp`) optimizes agent reasoning and LSP query alignment.
- **Tag Breakout Prevention**: AI models can generate content containing closing tags (`</ask_user_result>`). Neutralizing closing delimiters inside wrapped payloads prevents prompt injection attacks.
