# C++23 Development Guidelines Summary
- **Standard & Memory**: C++23. Strictly follow RAII (`make_unique`/`make_shared`, avoid raw `new`/`delete`). Use modern features (`std::format`, `std::string_view`, `std::span`, `constexpr`, `noexcept`, `#pragma once`, ranged for loops).
- **Security-First**: Adopt a security-first mindset across all generated code. Document and sanity-check untrusted input early and verify buffer/array bounds.
- **Organization**: Place each class in matching `.h`/`.cpp` files.
- **Comments**: Document rationale ("why"), thread ownership, and mutex locking rules.
- *Full guidelines & templates available at `system://languages/cpp23.md`.*