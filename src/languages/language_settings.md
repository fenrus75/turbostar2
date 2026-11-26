# `languages::language_settings`

## Goals & Constraints
- Centralize compiled-in per-language defaults used across editor and agent subsystems (starting with LLM sampling temperature).
- Honor the project's configured primary language (`config_manager::get_instance().get_primary_language()`).
- Return `std::nullopt` for languages that should use the provider/model default (so `"temperature"` is omitted from API payloads rather than hardcoded).
- Use `0.1` for `SystemVerilog` / `Verilog` to improve RTL determinism and reduce syntax/logic drift on models with high default temperatures.
- Guard OpenAI reasoning model prefixes (`o1`, `o3`, `o4`) via `model_supports_temperature()` because those endpoints return HTTP 400 if `"temperature"` is present.

## Lessons Learned
- Keep `language_settings.cpp` in `core_sources` (`libturbostar_core`) alongside `config_manager.cpp` so `libagentlib` protocol implementations can call it directly without circular dependencies.
