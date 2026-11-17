# `vcd_axi_protocol_checker`

Subclass of `vcd_protocol_checker` validating AXI4-Stream (`axi_stream`), AXI4-Lite (`axi_lite`), and full AXI4 (`axi4`) buses in post-simulation VCD traces.

## Goals & Constraints
- **Exact Bundle Matching**: Only binds when all required channel signals for the variant exist in the same VCD scope with a common prefix (`axi_lite` additionally rejects scopes containing `awlen`/`arlen`, which bind as `axi4`).
- **Shared Handshake Core**: Uses a single `check_handshake_channel` helper for `AXI_VALID_DROPPED`, `AXI_PAYLOAD_CHANGED`, `AXI_X_ON_VALID`, and `AXI_X_ON_PAYLOAD` across all 5 AXI channels and AXI4-Stream.
- **Reset-Aware**: Automatically skips checks while the scope's reset (`aresetn`/`rst_n`/`rst`/etc.) is asserted and on the first post-reset edge.

## Lessons Learned
- Initial implementation: `sample_wave` returns multi-bit hex values without a `0x` prefix (`default_radix='x'`), so `parse_vcd_uint` parses unprefixed values as base-16.
