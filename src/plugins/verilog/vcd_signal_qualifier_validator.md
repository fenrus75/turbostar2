# VCD Signal Qualifier Validator (`vcd_signal_qualifier_validator`)

## Goals & Responsibilities
Validates argument schemas and permissions for the `vcd_signal_qualifier` tool action.

Key validation checks:
1. **Sandbox Validation**: Checks read permission for `path` via `fs_security.validate_access()`.
2. **Flexible Invocations**:
   - Single binding (`signal` + `qualifier`)
   - Batch dictionary (`bindings`)
   - Query mode (omitting `qualifier` and `bindings`)
   - Clear mode (`clear: true`)
3. **C-Linkage Exports**: Exports `register_vcd_signal_qualifier()` and `unregister_vcd_signal_qualifier()` for dynamic plugin registration.

## Lessons Learned
- Allowing `signal` and `qualifier` to be omitted in query mode provides an ergonomic way for agents to inspect active signal bindings before sampling waveforms.
