# TurboStar editor

Top design documentation: `docs/design.md`


# Project specific rules

- keep `docs/design.md` and related documents updated at all times
- git commit after each logical change
- perform a code review before each commit to ensure no stray edits happened
- run the test suite before commit
- when fixing a bug, create a testcase BEFORE fixing the bug; the testcase
    should first fail, and pass once the bug is fixed.

# Dependencies
- CLI11 (header-only) for command-line parsing.

