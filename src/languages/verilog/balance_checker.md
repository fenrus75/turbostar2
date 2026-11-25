# `languages::verilog::balance_checker`

Verilog and SystemVerilog syntactic block balance checker invoked by `languages::balance_checker`.

## Goals & Constraints
- Tracks `begin`/`end`, `case`/`casex`/`casez`/`endcase`, `module`/`macromodule`/`endmodule`, and `{`/`}` concatenations.
- Ignores `//` and `/* ... */` comments, `"..."` strings, `\escaped ` identifiers, member/port connections (`.end(sig)`), and substrings inside identifiers (`begin_flag`, `endian`).
- Never treats `'` as a string delimiter so sized numeric literals (`1'b0`, `8'hFF`, `'0`) cannot corrupt scanning.

## Lessons Learned
- Checking for a preceding `.` prevents named port connections like `.end(sig)` from decrementing the `begin`/`end` counter.
