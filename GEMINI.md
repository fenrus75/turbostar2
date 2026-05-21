# TurboStar editor

Top design documentation: `docs/design.md`


# Project specific rules

- keep `docs/design.md` and related documents updated at all times
- git commit after each logical change or item implemented. This is a standing rule.
- perform a code review before each commit to ensure no stray edits happened
- run the test suite before commit
- when fixing a bug, create a testcase BEFORE fixing the bug; the testcase
    should first fail, and pass once the bug is fixed.
- when splitting a large source file into multiple files, always add a block comment at the top of the original file describing the new files and their general contents to aid discoverability.

## `docs/todo.md` specific rules
- This file is frequently edited by the human.
   - Re-read the file before working on TODO items or evaluating what to
     work on next.
   - Re-read the file after completion of a TODO item.
- Move completed items to the Done section.
   - Re-read the todo.md file before doing the edit!
- Add any items deferred during other activities to the short-term section.

# Dependencies
- CLI11 (header-only) for command-line parsing.


