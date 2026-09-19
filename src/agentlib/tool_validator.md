# Tool Validator (`tool_validator`)

## Goals
The `tool_validator` class is the abstract base class and schema definition interface for all agent tools in Turbostar. It defines the parameter schema, input normalization rules, documentation, tool family assignment, and validation contracts.

## Architecture and Constraints
- **Schema & Metadata**:
  - `get_name()`: Unique tool identifier.
  - `get_description()`: Clear, actionable description for model system prompts.
  - `get_parameters_schema()`: Valid JSON Schema object describing properties and required fields.
  - `get_family()`: Pipe-delimited tool family string (e.g. `"base"`, `"binary|hexedit"`).
- **Two-Stage Validation Contract**:
  - `validate_syntax(json_str, out_args, out_error)`: Validates types and required fields against JSON Schema. Automatically normalizes parameter aliases (e.g. `file_path` / `filepath` -> `path`).
  - `validate_runtime(ctx, out_error)`: Contextual runtime checks (e.g. verifying sandbox access with `ctx.fs_security` or checking working directory states).
- **Factory Interface**:
  - Creates executable `llm_tool` instances holding parsed, validated arguments.

## Lessons Learned
- **Robust Parameter Alias Normalization**: Different model families use varying casing and naming conventions (e.g. `path` vs `file_path` vs `filePath`, or `args` vs `arguments`). Normalizing aliases at the validator level prior to schema validation eliminates widespread hallucination errors without altering underlying tool logic.
- **Strict Separation of Syntactic vs Contextual Checks**: Syntactic validation requires no access to file systems or event queues; separating it from runtime checks allows batch argument parsing across parallel tool invocations.
