# Turbostar Release Checklist

Prior to making a release or release candidate, perform all of the
checks below to ensure a smooth, secure, and reliable experience for users.

> **Agent note:** When performing these steps, treat every section as
> a strict gate — do not proceed to the next section if any item in the
> current section fails.

---

## 1. Version Bump

- [ ] Determine the target version number with the project maintainer:
      - Initial release: e.g. `v0.1.0` (or `v1.0.0`)
      - Release candidate: e.g. `v0.1.0-rc1`
      - Patch / Minor update: e.g. `v0.1.1` or `v0.2.0`
- [ ] Update the version in `meson.build` (`version:` field in `project(...)`).
- [ ] Verify the version appears correctly in the binary output:
      ```bash
      build/turbostar --version
      ```

---

## 2. Dependency & Toolchain Verification

- [ ] Run `scripts/check_dependencies.sh` to verify vendor headers (e.g. `include/CLI11.hpp`) and version constraints.
- [ ] Verify required system development packages and tools:
      - C++23 compliant compiler (GCC >= 14 or Clang with libc++ >= 18)
      - Meson (>= 1.0) and Ninja
      - Shared libraries: `ncursesw`, `re2`, `nlohmann-json3`, `cpp-httplib`, `sqlite3`, `libunwind`, `libmagic`, `lexbor`, `graphicsmagick++`, `zydis`, `curl`, `openssl`
- [ ] Validate website and documentation HTML assets:
      ```bash
      python3 scripts/verify_html.py docs/index.html docs/editor.html docs/ai.html docs/details.html
      ```

---

## 3. Build Configurations & Sanity Matrix

The following Meson build configurations validate compiler compatibility, sanitizer instrumentation, and optimization levels:

| Build Type | Configuration Command | Purpose |
| :--- | :--- | :--- |
| **Standard** | `meson setup --wipe build` | Default development build with unit tests |
| **Release** | `meson setup --wipe --buildtype=release build_release` | Optimized release binary verification |
| **Debug** | `meson setup --wipe --buildtype=debug build_debug` | Full debug symbols with assertions |
| **Fast (No Tests)** | `meson setup --wipe -Denable-tests=false build_notests` | Core application build speed on constrained hardware |
| **ASAN/Gcov** | `meson setup --wipe -Db_sanitize=address -Db_coverage=true build_acov` | AddressSanitizer and code coverage analysis |
| **Gcov** | `meson setup --wipe -Db_coverage=true build_cov` | Clean code coverage baseline |
| **UBSAN** | `meson setup --wipe -Db_sanitize=undefined build_ubsan` | UndefinedBehaviorSanitizer checks |
| **MSAN** | `meson setup --wipe -Db_sanitize=memory build_msan` | MemorySanitizer (requires instrumented system libs) |
| **TSAN** | `meson setup --wipe -Db_sanitize=thread build-tsan` | ThreadSanitizer race condition detection |
| **Clang Thread Safety** | `CXX=clang++ CC=clang meson setup --wipe build_clang` | Clang compile-time thread safety analysis |
| **GCC Static Analysis** | `meson setup --wipe -Denable-analyzer=true build_analyzer` | GCC `-fanalyzer` checks (`ninja -j1`; run infrequently) |

---

## 4. Release Automation & Test Suite

- [ ] Run `scripts/run_release_builds.sh` to automate the verification across all standard build configurations.
- [ ] Ensure all unit and E2E tests pass.
      > **Policy**: There is an **absolute zero** tolerance for test failures for any release (including release candidates). If any test fails, inspect `build*/meson-logs/testlog.txt` to diagnose and fix the root cause before proceeding.

---

## 5. First-Launch & Out-of-the-Box Experience

Verify that TurboStar bootstraps cleanly in a completely blank user profile with no existing cache or configuration files:

- [ ] Execute an automated first-launch probe with an isolated `$HOME`:
      ```bash
      TMP_HOME=$(mktemp -d)
      HOME="$TMP_HOME" build/turbostar --exit-immediately 1.0
      EXIT_CODE=$?
      rm -rf "$TMP_HOME"
      test $EXIT_CODE -eq 0
      ```
- [ ] Confirm clean exit code `0` with no unhandled exceptions or missing directory crashes.

---

## 6. Multi-Persona & Symlink Verification

Turbostar installs multi-call symlinks in `bin/` (`turboagent`, `turboserver`, `turbomcp`) which alter its runtime personality based on `argv[0]`.

- [ ] Build the deployment staging directory:
      ```bash
      ninja -C build tests/copy_to_testrun
      ```
- [ ] Verify each executable persona responds correctly to CLI probes:
      - `testrun/turbostar --version` (Main TUI editor)
      - `testrun/turboagent --help` (Autonomous agent launcher)
      - `testrun/turboserver --help` (Headless A2A server mode)
      - `testrun/turbomcp --help` (Stdio MCP server protocol mode)
      - `build/turbostar-crashprocess --help` (Out-of-process crash reporting daemon)
      - `build/turbostar-semcode-indexer --help` (Semantic code indexer)

---

## 7. Dynamic Plugin Integrity

Verify that all built-in shared object plugins in `src/plugins/` compile and link cleanly with no unresolved symbols:

- [ ] Run `ldd` symbol check on all plugin binaries:
      ```bash
      for p in build/src/plugins/*.so; do
          echo "Checking plugin: $p"
          ldd -r "$p" 2>&1 | grep -E "undefined symbol|not found" && exit 1
      done
      ```

---

## 8. Memory Leak Check

- [ ] Valgrind full leak check (standard uninstrumented build):
      ```bash
      valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 build/turbostar --exit-immediately 1.0
      ```
      All leaks must be resolved prior to release.
- [ ] ASAN leak check (sanitizer instrumented build):
      ```bash
      ASAN_OPTIONS=detect_leaks=1 build_acov/turbostar --exit-immediately 1.0 2>&1 | grep -i "leak\|SUMMARY"
      ```
      Confirm zero memory leaks reported on clean termination.

---

## 9. Test Coverage

Coverage data is collected from `build_acov` (ASAN + gcov):

- [ ] Run the test suite to populate coverage data:
      ```bash
      ninja -C build_acov test
      ```
- [ ] Capture the coverage snapshot:
      ```bash
      bash scripts/coverage_report.sh <version-label> build_acov
      ```
- [ ] Review `coverage/index.html` for any significant regressions versus the previous baseline.

---

## 10. Packaging & Installation Verification

- [ ] Staging install via `DESTDIR`:
      ```bash
      TMP_DEST=$(mktemp -d)
      DESTDIR="$TMP_DEST" ninja -C build_release install
      ```
      Verify files install into the expected layout:
      - `usr/local/bin/turbostar`
      - `usr/local/bin/turboagent` (symlink -> `turbostar`)
      - `usr/local/bin/turboserver` (symlink -> `turbostar`)
      - `usr/local/bin/turbomcp` (symlink -> `turbostar`)
      - `usr/local/bin/turbostar-crashprocess`
      - `usr/local/bin/turbostar-semcode-indexer`
      - `usr/local/libexec/turbostar/libturbocatch.so`
      ```bash
      rm -rf "$TMP_DEST"
      ```
- [ ] Source distribution tarball (`meson dist`):
      Verify that an end user can build directly from an extracted release tarball without `.git`:
      ```bash
      meson dist -C build_release --no-tests
      DIST_TAR=$(ls build_release/meson-dist/turbostar-*.tar.xz)
      TMP_DIST=$(mktemp -d)
      tar -xf "$DIST_TAR" -C "$TMP_DIST"
      meson setup "$TMP_DIST"/turbostar-*/build_tarball "$TMP_DIST"/turbostar-* -Denable-tests=false
      ninja -C "$TMP_DIST"/turbostar-*/build_tarball
      rm -rf "$TMP_DIST"
      ```

---

## 11. Release Notes

Store release notes in `docs/relnotes.md`.
For the first release, summarize the initial architecture and capabilities. For subsequent releases, use `git shortlog <prev-stable-tag>..` to enumerate changes.

- [ ] Update `docs/relnotes.md` with:
      - Version tag and release date (e.g. `## v0.1.0 (YYYY-MM-DD)`)
      - Top user-visible highlights:
        - TUI Editor: Turbo Pascal aesthetic, WordStar/Joe keybindings, LSP auto-detection, split compile/test panes.
        - AI Agentic Architecture: In-editor agent chat (`^KA`), virtual paged context, atomic undo/redo, crashdump analysis.
        - Tool System & Protocols: MCP server (`turbomcp`), A2A server (`turboserver`), dynamic plugins.
      - Summary of new tools, plugins, and keybindings.
      - Summary of CLI flags and configuration options.
- [ ] Thank external contributors:
      ```bash
      git log --format="%an <%ae>" <prev-tag>.. | sort -u
      ```

---

## 12. Tagging & Publishing

> **Agent note:** Only perform this section after explicitly confirming
> with the project maintainer that all checks above have passed and they approve tagging.

- [ ] Confirm with the user that all checks passed and obtain approval to tag.
- [ ] Create annotated git tag:
      ```bash
      git tag -a vX.Y.Z -m "Release vX.Y.Z"
      ```
- [ ] Push git tag to origin:
      ```bash
      git push origin vX.Y.Z
      ```