# Agent Interaction Base (`agent_interaction`)

## Goals
The `agent_interaction` class (`src/agentlib/interactions/base.h`, `src/agentlib/interactions/base.cpp`) serves as the abstract base class and core rendering pipeline for all visual turns and messages displayed inside the conversational LLM agent sidebar.
Its responsibilities include:
1. **Turn Layout & Wrapping**: Reflowing long model outputs, markdown text, and code blocks to the current window width using `wrap_text`.
2. **Embedded Syntax Highlighting**: Identifying embedded markdown code blocks (` ```lang `) and routing them to the appropriate `highlighter` instance.
3. **Glyph & Attribute Mapping**: Storing color pairs, text attributes (bold, underline), and box borders for terminal presentation.

## Architecture and Constraints
- **Language Detection**: Fenced code block language identifiers are normalized with `utf8::to_lower` to ensure accurate lookup against `highlighter_registry`.
- **Character Boundaries**: Text wrapping respects multi-byte UTF-8 character boundaries and terminal column display widths.

## Lessons Learned
- **Code Block Language Normalization**: Using `utf8::to_lower` prevents signed char undefined behavior and locale bugs when matching language identifiers (e.g. ````CPP```` vs ````cpp````).
