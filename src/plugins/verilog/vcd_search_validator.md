# VCD Search Validator (`vcd_search_validator`)

## Goals & Responsibilities
Validates argument schemas, permission checks, and bounds for the `vcd_search` tool action.

Key validation checks:
1. **Sandbox Permission**: Verifies read access to `path` via `fs_security.validate_access()`.
2. **Required Fields**: Enforces presence of `path`, `signal`, and `value`.
3. **Enum & Flag Validation**:
   - `edge`: must be `posedge`, `negedge`, or `both`
   - `direction`: must be `forward` or `backward`
   - `first` / `last`: mutually exclusive boolean shortcuts mapping to `limit=1` with forward / backward directions.
4. **Tool Registration**: Provides `extern "C"` lifecycle hooks (`register_vcd_search`, `unregister_vcd_search`).

## Lessons Learned
- Providing `first` and `last` as convenience boolean shortcuts simplifies common agent queries ("when does this first happen?" or "what was the last value?") while maintaining an underlying `direction` + `limit` implementation.
