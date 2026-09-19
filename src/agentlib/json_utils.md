# JSON Parameter Utilities (`json_utils`)

## Goals
The `json_utils` namespace provides resilient parameter extraction, numeric coercion, and type-safe decoding for tool argument JSON payloads. Because language models frequently output numeric arguments in diverse representations (integers, floats, string-encoded numbers, or hex addresses), these utilities ensure tools parse inputs reliably without crashing.

## Architecture and Constraints
- **Flexible Numeric Extraction (`get_number<T>`)**:
  - Automatically parses native unsigned integers, signed integers, and floating-point values into destination type `T`.
  - Coerces string-encoded numbers, trimming leading and trailing whitespace.
  - Automatically recognizes and converts hexadecimal strings prefixed with `0x` or `0X` (common when models call memory or hex tools).
  - Validates against out-of-range overflows and non-numeric garbage, returning descriptive error messages.
- **Convenience Fallbacks**:
  - `parse_numeric_from_json`: Returns a default fallback value if a parameter is missing or invalid, ideal for optional parameters like line limits or offset counts.

## Lessons Learned
- **Hex Address Coercion**: Models invoking binary or reverse-engineering tools (`hexdump`, `hexinspect`, `x86_disassemble`) frequently pass hex string offsets like `"0x401000"` instead of raw decimal integers. Auto-detecting hex prefixes prevents tool schema validation errors.
- **Whitespace Tolerance**: LLM tokenizers often introduce stray leading/trailing spaces into JSON string parameters (e.g. `" 123 "`). Trimming whitespace before running `std::strtoull` or `std::strtod` avoids spurious parsing failures.
