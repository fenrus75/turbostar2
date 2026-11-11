# verilog_compile_validator

## Goals & Constraints

- **Purpose**: Validates arguments for `verilog_compile`, ensuring safe module identifiers or file paths and verifying write access for `output_file` (defaulting to `tmp://synthesis.log`).
- **Flexible Parameter Aliases**: Accepts `file`, `path`, `top_module`, `top`, or `rtl_module` as aliases for `module`, and `log_file`, `log`, or `out` as aliases for `output_file`.

## Lessons Learned

- **Default VFS Log Output (`tmp://synthesis.log`)**: Validating `"tmp://synthesis.log"` through `ctx.fs_security.validate_access(..., access_type::write, ...)` ensures that the default log destination adheres to the same sandbox and VFS security policies as user-supplied paths.
