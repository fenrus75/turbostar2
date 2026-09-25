# fs_replace_engine

## Overall Goals and Architecture
`fs_replace_engine` provides the shared execution backend for `fs_replace_content` and `fs_multi_replace_content`.
It handles:
1. Strict and relaxed string matching (including whitespace normalization and indentation tolerance).
2. Disambiguation using line hints and enclosing function/symbol hints.
3. Syntactic brace balancing checks (detecting newly introduced unbalanced curly braces at file scope or within enclosing functions/methods).
4. Atomic file replacement via temporary files and rename semantics.

## Constraints
- File access must be properly validated and resolved by security managers before replacement.
- Edits must preserve existing line endings and formatting where possible.
- Brace-balance checks must verify whole-file balance and avoid false-positive warnings when the file already had pre-existing imbalances outside the edited scope.

## Lessons Learned
- **Parallel Test Isolation**: Tests exercising `fs_replace_content` and `fs_multi_replace_content` must isolate temporary test file names by process ID (`getpid()`). When tests share fixed paths like `project_root + "/tmp/test_replace_content.txt"`, parallel test execution (such as `meson test` running multiple suites or concurrent runs) causes concurrent writes to the same file. This leads to intermittent race conditions where symbol lookup or brace validation reads file content modified by another process.
