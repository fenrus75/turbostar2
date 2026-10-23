# Source Token (`languages::source_token`)

## Goals
The `languages::source_token` structure (`src/languages/source_token.h`) provides a unified, canonical data structure representing extracted source code identifiers across all language-specific parsers, token extractors, and language servers:
1. **Consolidated Representation**: Unifies disparate token structs (`candidate_type_token`, `candidate_call_token`, etc.) into a single well-defined type.
2. **Positional Context**: Tracks the 1-based source line number and 0-based character column offset to support precise symbol queries, hover resolutions, and cache lookups.
3. **Language Neutrality**: Serves as the universal currency between language frontends (C++, Python, etc.) and editor tooling (type definition prefetchers, call resolvers).

## Architecture and Constraints
- **Value Semantics**: Lightweight aggregate struct with standard member types (`std::string name`, `int line`, `int col`).
- **Zero Framework Overhead**: Free of UI, document, or thread-locking dependencies.

## Lessons Learned
- **Consolidation**: Having distinct identical structs across extractors led to unnecessary type divergence and awkward aliases across namespaces. Unifying to `languages::source_token` streamlines downstream callers.
