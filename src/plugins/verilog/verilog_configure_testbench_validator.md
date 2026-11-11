# verilog_configure_testbench_validator

## Goals & Constraints

- **Purpose**: Validates arguments for `verilog_configure_testbench`, enforcing sandbox path checks on `testbench` and write permissions on `turbostar.json` when mutating associations.
- **Flexible Schema**: Accepts `modules` as either a single string or an array of strings (with alias `module`), normalizes `level` (`quick`, `comprehensive`, `extreme`), and supports `remove` (aliases `delete`, `clear`).
- **Conditional Purity**: Overrides `is_pure(untrusted_args)` to return `true` when invoked with no mutation arguments (list/inspect mode).

## Lessons Learned

- **Allowing Removal of Deleted Testbench Files**: When `remove: true` is passed, the validator checks `validate_access` for path safety without requiring `fs_utils::is_regular_file(safe_tb)` to succeed, so stale entries for deleted testbench files can still be cleaned up from `turbostar.json`.
