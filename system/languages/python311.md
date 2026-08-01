# Python 3.11 Development Guidelines

## Code Conventions
- **Language Standard**: Python 3.11+.
- **Style Guide**: Follow PEP8 style guide and naming conventions.
- **Type Annotations**: Consistently add PEP 484 type annotations (`type_a | type_b`, `list[str]`, `dict[str, int]`) to all function arguments, return values, and variable declarations.
- **Virtual Environments**: Prefer running Python scripts within isolated environments or VFS `tmp://` scratch spaces.

## Security Considerations
- **Security Validation**: Code executed or created must pass `bandit` security checks (no `shell=True` in subprocess calls, no unvalidated `eval()` or `exec()`).
- **Input Sanitization**: Validate all external paths, user input strings, and API payloads before processing.
