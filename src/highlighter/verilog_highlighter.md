# Verilog & SystemVerilog Syntax Highlighter (`verilog_highlighter`)

## Overview & Goals
The `verilog_highlighter` parses and styles IEEE 1364 Verilog and IEEE 1800 SystemVerilog source files (`.v`, `.sv`, `.vh`, `.svh`). It colors hardware description keywords, system tasks (`$display`, `$finish`), compiler directives (`` `timescale ``, `` `include ``), string literals, block comments, and line comments (`//`).

## Key Responsibilities
- **File & Language Matching**: Recognizes `.v`, `.sv`, `.vh`, `.svh` and languages `verilog`, `v`, `systemverilog`, `sv` (case-insensitive).
- **Keyword & Directive Extraction**: Uses regular expressions to match keywords (`module`, `always_ff`, `logic`, etc.) and system tasks / macro directives (`$*`, `` `* ``).
- **String Literals & Comments**: Handles strings (`"..."`), block comments (`/*...*/`), and line comments (`//...`).

## Constraints & Security Considerations
- **Untrusted HDL Sources**: Source text may contain deeply nested constructs, escaped identifiers, and URLs or `//` sequences inside strings.
- **Bounds Checking**: Enforces `char_comment_pos < attrs.size()`.

## Lessons Learned
- **Multiple `//` Sequences on a Single Line**: When strings contain `//` (such as URLs in display messages), a single `find("//")` check skips the actual line comment later in the line. Iterating past string/comment boundaries ensures the true comment position is identified.
