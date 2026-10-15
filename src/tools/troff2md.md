# troff2md - Troff to Markdown Parser

## Overview & Goals
`troff2md` converts manual page formatting source (troff / groff / roff) into clean, standard Markdown. It is used by `fs_man` and the internal tools registry to present man pages to LLM agents and human users.

## Constraints & Security Requirements
- **Untrusted Input**: Troff content can originate from arbitrary local man pages, downloaded packages, or external sources. The parser must never crash, throw unhandled exceptions, exhaust memory, or enter infinite loops.
- **Bound Indentation**: Man page nesting (`.RS` / `.RE` and `.in`) must be bounded to prevent denial-of-service via huge strings or negative indentations. Current indent is clamped to `[0, 80]`, and nesting depth is capped at 64 levels.
- **Safe Character Processing**: Any call to `std::isspace` or `std::isdigit` must explicitly cast characters to `static_cast<unsigned char>` to prevent undefined behavior on non-ASCII / UTF-8 multi-byte bytes.
- **Resilient Termination**: Unclosed code blocks (`.EX` / `.nf`) and unclosed tables (`.TS` without `.TE`) must be automatically closed and flushed at EOF rather than losing content.

## Architecture
- `split_lines`: Splits input by newline into line chunks using `std::string_view`.
- `dispatch_table`: Maps structural troff directives (`.TH`, `.SH`, `.SS`, `.B`, `.I`, `.BR`, `.BI`, `.IR`, `.TP`, `.IP`, `.EX`, `.EE`, `.RS`, `.RE`, `.TS`, etc.) to handler functions.
- `process_escapes`: Resolves troff escape sequences (`\fB`, `\fI`, `\fR`, `\-`, `\e`, `\&`, `\(aq`, `\(dq`, `\(bu`, `\(co`, `\(em`, `\(en`).
- `process_table`: Renders simple tabular data as Markdown tables, or complex tables as fenced text blocks. Escapes literal `|` characters in Markdown table cells.

## Lessons Learned
- **Negative Indent String Allocation Crash**: Commands like `.in +-5` or `.RS +-10` resulted in negative indentation values in `indent_stack`. When constructing `std::string(state.current_indent(), ' ')`, negative signed integers converted to enormous `size_t` values, triggering `std::length_error` (`basic_string::_M_create`) and aborting the process. Strict token validation and clamping indents to `[0, 80]` resolved this vulnerability.
- **UTF-8 Ctype Safety**: Passing signed `char` with values $\ge 0x80$ to `std::isspace` or `std::isdigit` causes undefined behavior in standard C/C++. All character classification calls must use `static_cast<unsigned char>`.
- **EOF Table Flushing**: If a troff file was truncated with an unclosed `.TS`, the accumulated table buffer was previously discarded. Explicitly flushing `state.table_lines` at EOF preserves table content.
- **Markdown Table Pipe Escaping**: Literal pipe characters `|` inside table cell data split Markdown columns inappropriately unless escaped as `\|`.
