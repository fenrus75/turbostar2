# `inspector_helpers` and `markdown_builder`

## Goals
- Provide reusable formatting utilities and a fluent Markdown Lite builder for inspector providers.
- Standardize formatting of headers, code blocks, parameter numbering, and declaration links across all providers.
- Eliminate duplicated string formatting and manual markdown tag concatenation across different provider implementations.

## Constraints
- Must remain 100% free of ncurses or UI headers.
- All methods must be non-allocating where feasible, appending efficiently to an internal string buffer.
- Produced markdown must adhere strictly to the companion window's Markdown Lite specification.

## Lessons Learned
- Providing a shared fluent builder prevents subtle formatting errors across independent providers (e.g. inconsistent spacing around headers, or unescaped backticks).
