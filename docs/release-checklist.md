# Turbostar release checklist

Prior to making a release or release candidate, perform all of the
checks below to ensure a smooth experience for our users.

> **Agent note:** When performing these steps, treat every section as
> a gate — do not proceed to the next section if any item in the
> current section fails.

## Version bump

- [ ] Ask the user for the new version number.
      Suggest at least two options (use `git tag` to check the current latest):
      1. Next minor/major release (e.g. `v2.X`)
      2. Next release candidate (e.g. `v2.X-rc1`)
- [ ] Update the version in `meson.build` (`version:` field)
- [ ] Verify the version appears correctly in `turbostar --version` output

## Dependency verification
- [ ] Run `scripts/check_dependencies.sh` to verify all required dependencies (e.g., `include/CLI11.hpp`) are present and meet version requirements.

## Build configurations

The following Meson configuration options are used for the release validation builds:

| Build Type | Configuration Command |
| :--- | :--- |
| **Standard** | `meson setup --wipe build` |
| **Release** | `meson setup --wipe --buildtype=release build_release` |
| **Debug** | `meson setup --wipe --buildtype=debug build_debug` |
| **ASAN/Gcov**| `meson setup --wipe -Denable-tests=true -Db_sanitize=address build_acov` |
| **Gcov** | `meson setup --wipe -Denable-tests=true build_cov` |
| **UBSAN** | `meson setup --wipe -Denable-tests=true -Db_sanitize=undefined build_ubsan` |
| **TSAN** | `meson setup --wipe -Denable-tests=true -Db_sanitize=thread build-tsan` |

## Release automation & validation
- [ ] Run `scripts/run_release_builds.sh` to automate the verification of all required build types (Standard, Release, Debug, ASAN/Gcov, UBSAN).
- [ ] Ensure all builds and tests pass within the automation script.

## Memory leak check

- [ ] Valgrind full leak check (use the uninstrumented standard build):
      ```
      valgrind --leak-check=full --show-leak-kinds=all build/turbostar --exit-immediately
      ```
      Any new leaks must be fixed before release.

- [ ] ASAN leak check (confirms the same with sanitizer instrumentation):
      ```
      ASAN_OPTIONS=detect_leaks=1 sudo -E build_acov/turbostar --exit-immediately 2>&1 | grep -i "leak\|SUMMARY"
      ```
      Only the known 21-byte libpci leak is acceptable.

## Test coverage

Coverage is collected from `build_acov` (ASAN + gcov), which gives the most
instrumentation. Run the test suite first, then the `--once` run to cover the
display/hardware paths that tests cannot reach.

- [ ] Run the test suite to populate coverage data:
      `ninja -C build_acov test`
- [ ] Run a live measurement to cover display and hardware paths:
      `ASAN_OPTIONS=detect_leaks=0 sudo -E build_acov/turbostar --once --time=3`
- [ ] Capture the combined coverage snapshot:
      `bash scripts/coverage_report.sh <version-label> build_acov`
- [ ] Review the report for any significant regressions vs. the previous release.

## Release notes

Store in `docs/relnotes.md`. Use `git shortlog <prev-stable-tag>..` to enumerate commits,
where `<prev-stable-tag>` is the **last stable release** (e.g., `v2.15`), not the previous RC.
This ensures the notes are cumulative and cover all RC changes as well.
For the v2.16 cycle the base was `v2.15`.

**RC cadence**: the file has a single section for the whole release cycle. When moving from
`-rc1` to `-rc2` (or `-rc3`, etc.) **update the heading and add new items in-place** — do not
create a new section.  The final stable tag just renames the heading (e.g., `## v2.16-rc2` →
`## v2.16`).

- [ ] Update the version heading in `docs/relnotes.md` to match the new tag
      (for RCs: add new changes to the existing sections; do not duplicate)
- [ ] Update the "Recent releases" table near the top of `README.md`
      with the new version and a one-line summary
- [ ] Summary of top user-visible enhancements and changes
      (new tunables, UI changes, other new features)
- [ ] **New tunables**: users care about these most — enumerate explicitly.
      Find additions with:
      ```
      git diff <prev-stable-tag>.. -- src/tuning/tuning.cpp | grep "^+.*add_sysfs_tunable"
      ```
      List each new tunable with its description, sysfs path, and suggested value.
      Also check for wildcard-path tunables (paths containing `*`) which apply to
      multiple devices at once.
- [ ] Summary of new command-line options
- [ ] **Short** summary of internal changes (no more than 3 lines for all changes combined)
- [ ] **Thank external contributors**: run `git shortlog -sn <prev-stable-tag>..` and add a
      "Thanks" section listing anyone who is not the project maintainer (Arjan van de Ven).
      Use `git log --format="%an <%ae>" <prev-stable-tag>.. | sort -u` to get full name+email.

## Tagging the release

> **Agent note:** Only perform this section after explicitly confirming
> with the user that all checks above have passed and they are ready to tag.

- [ ] Confirm with the user that all checks passed and they approve tagging
- [ ] `git tag -a vX.Y -m "Release vX.Y"`
- [ ] `git push origin vX.Y`
