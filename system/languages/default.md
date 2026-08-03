# General Development Guidelines

## Security Considerations
- **Security-First Mindset**: Adopt a security-first mindset across all generated code.
- **Untrusted Input**: Clearly label variables and parameters holding untrusted (user/network) data in comments.
- **Early Validation**: Sanity-check and validate all untrusted input as early as possible.
- **Bounds Checking**: Verify all buffer and array accesses against underflows and overflows.

## Code Commenting Conventions
- **Intent over Logic**: Focus comments on rationale (the "why") and design goals rather than restating code implementation logic.
- **Ownership & Constraints**: Document ownership, memory lifecycles, thread-safety expectations, and function constraints.
- **Synchronization Declarations**: Precede every mutex or lock declaration in header files with a comment block explaining:
  1. What specific data or resources the mutex protects.
  2. Locking rules, lifecycle, or ordering guidelines.
