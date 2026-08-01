# Python 3.11 Coding Guidelines

## Core Principles
- **Language Version**: Python 3.11+. Use modern type hinting (`type_a | type_b`, `list[str]`, `dict[str, int]`).
- **Security Validation**: Code executed or created must pass `bandit` security checks (no `shell=True` in subprocess calls, no unvalidated `eval()`).
- **Virtual Environments**: Prefer running Python scripts within isolated environments or VFS `tmp://` scratch spaces.
