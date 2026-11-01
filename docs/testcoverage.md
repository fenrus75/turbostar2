# Test Coverage Guide

This document explains how to generate, read, and understand the test coverage reports for the Turbostar project. We strive for high test coverage, particularly in the core logic components (`document`, `editor`, `line`).

## Prerequisites

To generate a test coverage report, your system must have the following tools installed:
*   `gcov` (part of the GCC toolchain)
*   `gcovr` (a utility to generate summaries from `gcov` data)

On Debian/Ubuntu:
```bash
sudo apt-get install gcc gcovr
```

## Generating the Report

Test coverage is integrated into our Meson build system. To run the analysis, you must configure a dedicated build directory with coverage instrumentation enabled.

1.  **Configure the coverage build:**
    ```bash
    meson setup build-coverage -Db_coverage=true
    ```
    *Note: It is highly recommended to use a clean directory specifically for coverage to avoid polluting your development builds with heavy instrumentation.*

2.  **Compile the project:**
    ```bash
    ninja -C build-coverage
    ```

3.  **Run the test suite:**
    ```bash
    meson test -C build-coverage
    ```
    This step executes all unit and end-to-end (E2E) tests, generating the raw `.gcda` files containing the execution data.

4.  **Generate the summary report:**
    ```bash
    ninja -C build-coverage coverage-text
    ```
    This generates a human-readable text report. Meson also supports `coverage-html` and `coverage-xml` if you prefer a web-based or machine-readable format.

5.  **View the report:**
    ```bash
    cat build-coverage/meson-logs/coverage.txt
    ```

## Understanding the Output

The coverage report provides a breakdown by file, showing:
*   **Lines:** Total executable lines of code.
*   **Exec:** Number of lines executed during the test run.
*   **Cover:** The percentage of lines executed.
*   **Missing:** Specific line numbers that were *not* executed.

### Example (from May 2026)

```text
------------------------------------------------------------------------------
                           GCC Code Coverage Report
------------------------------------------------------------------------------
File                                       Lines    Exec  Cover   Missing
------------------------------------------------------------------------------
src/dialog.cpp                               103      39    37%   64-65,...
src/document.cpp                             834     659    79%   11,13-16,...
src/editor.cpp                               493     374    75%   31,75-76,...
src/file_dialog.cpp                          348     225    64%   16-18,...
src/find_dialog.cpp                          328     230    70%   13,80,...
src/line.cpp                                 206     155    75%   11,13-16,...
src/main.cpp                                  57      56    98%   35
src/menu_bar.cpp                             195     168    86%   42,74,...
src/window.cpp                               252     242    96%   42,154,...
------------------------------------------------------------------------------
```

## Measuring Coverage for Plugins

Plugin source files (e.g. `src/plugins/verilog/`) can be compiled directly into plugin-specific unit test executables (such as `tests/test_vcd_tools`). When testing plugins, you do not need to run the full E2E or editor test suites, which can take several minutes.

### 1. Build and Run Specific Plugin Tests
```bash
# Build the specific test target in build-coverage
ninja -C build-coverage tests/test_vcd_tools

# Clean stale profile data to prevent checksum mismatch warnings
find build-coverage -name "*.gcda" -delete

# Run the test binary to generate fresh .gcda files
./build-coverage/tests/test_vcd_tools
```

### 2. Generate Filtered Plugin Coverage Summary
To view coverage specifically for a plugin directory without noise from the rest of the project, use `gcovr` with the `--filter` flag:
```bash
gcovr --filter 'src/plugins/verilog' build-coverage
```

### Notes & Learnings for Plugin Coverage:
*   **Object File Locations**: When source files from `src/plugins/<name>/` are included in unit test executables under `tests/`, Meson generates their `.gcda` files under `build-coverage/tests/<test_target>.p/.._src_plugins_<name>_*.gcda`. Running `gcovr` on `build-coverage` discovers them automatically.
*   **Preventing Checksum Mismatches**: If you modify source code and recompile without deleting existing `.gcda` files, GCC's `libgcov` will emit warnings (`overwriting an existing profile data with a different checksum`) and discard counts. Always delete `.gcda` files before running the updated test binary.
*   **C Export Hooks**: Plugin tools often implement `extern "C" void register_<tool>()` and `unregister_<tool>()` hooks for dynamic loading (`plugin.cpp`). Unit tests should invoke these C entry points directly to ensure full plugin lifecycle coverage.
*   **Targeted Validation Edge Cases**: Tool validators (`*_validator.cpp`) contain numerous error-handling branches for parameter sanitization (e.g., negative limits, invalid enum values, missing required fields). Writing compact table-driven tests for these invalid argument permutations rapidly boosts plugin coverage.

## Coverage Goals & Known Gaps

Currently, our core TUI rendering (`window.cpp`) and entry points (`main.cpp`) have excellent coverage (>95%). However, UI-heavy components like dialogs naturally have lower coverage due to the difficulty of testing every edge case of user interaction in an E2E environment.

**Areas for Improvement (as of May 2026):**
*   `src/dialog.cpp` (37%): General dialog base classes and simple inputs.
*   `src/file_dialog.cpp` (64%): Error handling paths (e.g., inaccessible directories, empty filenames).
*   `src/find_dialog.cpp` (70%): Deep edge cases in advanced search parameter combinations.

When adding new features or fixing bugs, developers should consult the coverage report to ensure their new code paths are being exercised by the test suite.
