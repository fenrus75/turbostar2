# TurboStar editor

Top design documentation: `docs/design.md`


# Project specific rules

- keep `docs/design.md` and related documents updated at all times
- git commit after each logical change
- perform a code review before each commit to ensure no stray edits happened
- run the test suite before commit
- when fixing a bug, create a testcase BEFORE fixing the bug; the testcase
    should first fail, and pass once the bug is fixed.

## `docs/todo.md` specific rules
- this file is frequently edited by the human
   - re-read the file before working on todo items or evaluating what to
     work on next
   - re-read the file after completion of a todo item 
- move completed items to the done section
- add any items deferred during other activities to the short term section

# Dependencies
- CLI11 (header-only) for command-line parsing.


