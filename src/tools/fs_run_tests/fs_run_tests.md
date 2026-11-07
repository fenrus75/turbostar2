# Run Tests Tool (`fs_run_tests_tool`)

## Goals
`fs_run_tests_tool` executes the project's test suite (or specific requested tests) via Meson (`meson test`), CMake (`ctest`), or Make (`make test`), capturing output, filtering boilerplate build noise, and reporting any newly generated crash dumps.

## Constraints & Invariants
- Supports both single test names and arrays of multiple test names (`test_names`) so multiple related tests can be compiled and executed in a single invocation without build lock contention.
- Uses 2-tier resolution (`resolve_test_names_detailed`): exact/substring matching followed by tokenized matching (ignoring boilerplate tokens like `test` and `turbostar`).
- Always enables `--print-errorlogs` (Meson) and `--output-on-failure` (CMake) so failing tests surface their stderr/stdout regardless of how many tests were run, bounded by a 10,000-character output cap.
- If a batch of requested test names contains both resolved and unresolved entries, the resolved tests are executed and the unresolved names (with fuzzy-match suggestions when available) are reported at the end of the tool result.

## Lessons Learned
- **Always Enable `--print-errorlogs` / `--output-on-failure`**: Restricting `--print-errorlogs` to single-test runs (`resolved.size() == 1`) caused multi-test batch runs to hide failure output whenever one test in a batch failed, forcing the agent to re-run the failing test individually. Since `--print-errorlogs` only emits logs for failing tests and the total output is capped at 10 KB, enabling it unconditionally ensures multi-test vector runs are immediately actionable on failure.
- **Reporting Partial Resolution Misses**: When passing multiple test names in `test_names`, silently dropping unresolved entries whenever at least one test resolved could mislead the agent into believing all requested tests ran and passed. Refreshing the test-list cache when any query is unresolved and appending a warning listing unresolved queries after running the resolved tests prevents silent test skips.
