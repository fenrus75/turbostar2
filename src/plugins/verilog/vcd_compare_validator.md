# VCD Compare Validator (`vcd_compare_validator`)

## Goals & Responsibilities
Validates argument schemas, file permissions, and filtering parameters for the `vcd_compare` tool action.

Key validation checks:
1. **Sandbox Permission Checks**: Verifies read access to both `path_a` and `path_b` via `fs_security.validate_access()`.
2. **Required Fields**: Enforces presence of both `path_a` and `path_b`.
3. **Filtering & Modes**: Validates optional filters (`signals`, `scope`, `clock_signal`), clock edge modes (`posedge`, `negedge`, `both`), and boolean flags (`strict`, `ignore_invalid`).
4. **Tool Registration**: Exposes `extern "C"` hooks (`register_vcd_compare`, `unregister_vcd_compare`).

## Lessons Learned
- Comparing traces often involves comparing files across different subdirectories (e.g. `tests/fixtures/golden.vcd` vs `build/sim/run.vcd`); validating both paths against the security manager independently ensures sandboxing integrity across arbitrary project paths.
