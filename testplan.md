# agentlib Unit Test Plan

> **Status**: Inventory & gap-analysis (initial)
> **Last updated**: 2026-08-12
> **Purpose**: Restart point for the "add unit tests for key agentlib classes/methods" effort. If we crash or lose context, resume from here.

## Conventions / House Rules

- **Every new test file MUST start with a header comment** that lists:
  1. the target source file(s) it covers, and
  2. the exact method/function names it intends to cover.
- New `.cpp` test files must be registered in `meson.build` (an `executable()` + `test(...)` entry, suite: `'agent'`).
- Every test must call `test_watchdog::setup_watchdog(...)` at the start of `main()` — this isolates `HOME`, preventing parallel-test races and user-config clobbering. For extra isolation use `test_watchdog::scoped_test_home guard("prefix")`.
- Bug fixes: write a failing testcase FIRST, then fix.
- Run the full test suite before committing; commit after each logical change.

## How to resume

1. Re-read this file.
2. Pick the highest-priority file from the **Priority backlog** table below.
3. Check the corresponding existing tests already listed under "Existing coverage".
4. Write the new test file (with header comment), register it in `meson.build`, build, run.
5. Update this file (mark item done, move to Done section).
6. Commit.

---

## 1. Inventory — src/agentlib implementation files & public methods

Legend:
- ✅ = existing unit test coverage exists
- 🔴 = NO direct unit test / effectively uncovered
- ⚠️ = partial / incidental coverage (via other tests or tool-level)

### 1.1 ai_agent (src/agentlib/ai_agent.h / ai_agent.cpp — 3322 lines, the big one)

Public methods (from header):

| Method | Coverage | Where exercised |
|---|---|---|
| `create` | ✅ | ~30 test files call `ai_agent::create(...)` |
| `~ai_agent` | ✅ | all agent tests (destructor) |
| `submit_prompt` | ⚠️ | `test_report_final_result.cpp:78`, `template_tool_test.cpp.example` (template only) |
| `inject_context` | ✅ | `test_agent_compress_history.cpp`, `test_exit_summarization.cpp`, others |
| `replace_tool_result` | 🔴 | none found |
| `cancel_current_task` | 🔴 | none found |
| `close` | 🔴 | none found directly (implicit via dtor) |
| `get_id` / `get_name` | ⚠️ | incidental |
| `get_status` | ⚠️ | incidental |
| `get_current_tool` | 🔴 | none found |
| `set_status` | 🔴 | none found directly |
| `wait_until_idle` | ✅ | `test_get_subagent_output`, `test_kill_subagent`, `test_perform_code_review`, `test_report_final_result`, `test_send_message`, `test_wait_for_subagent` |
| `get_waiting_on_id` | 🔴 | none found |
| `spawn_subagent` | ✅ | see subagent/proc tools tests |
| `remove_subagent` | 🔴 | none found directly |
| `get_subagents` | ✅ | subagent tests |
| `find_agent_by_id` | ⚠️ | incidental |
| `get_all_active_agents` | 🔴 | none found |
| `clear_conversation` | ✅ | `test_clear_command.cpp` |
| `set_model` / `get_model` | ⚠️ | incidental |
| `get_tokens_tx/rx/cached/active` | 🔴 | none directly |
| `get_compaction_segments` | 🔴 | none directly |
| `get_current_system_prompt` | 🔴 | none directly |
| `get_estimated_cost` | 🔴 | none directly |
| `get_last_boundary_prob` | 🔴 | none directly |
| `get_last_inference_duration_ms` | 🔴 | none directly |
| `add_active_skill` / `activate_skill` / `get_active_skills` | ✅ | `test_activate_skill.cpp` |
| `add_active_tool_family` / `get_active_tool_families` / `is_tool_family_active` | ✅ | `test_activate_tool_family.cpp` |
| `increment_stat` / `get_stats` | ⚠️ | used via dump-state in agentcli, not unit-tested directly |
| `get_interactions` / `add_interaction` | 🔴 | none directly |
| `is_read_only` / `set_read_only` | ⚠️ | `test_agent_compress_history.cpp` |
| `get_role` / `set_role` | 🔴 | none directly |
| `get_properties` / `set_properties` | 🔴 | none directly |
| `get_allowed_write_file` / `set_allowed_write_file` | 🔴 | none directly |
| `is_mutation_possible` | 🔴 | none directly |
| `set_parent` / `get_parent` | ⚠️ | incidental in subagent tests |
| `get_global_queue` | 🔴 | none directly |
| `save_conversation` | 🔴 | none directly |
| `page_out_context` | ✅ | `test_agent_compress_history.cpp`, `test_exit_summarization.cpp` |
| `page_out_prior_context` | 🔴 | none found |
| `snapshot_episode` | 🔴 | none found |
| `update_episode_hint` | 🔴 | none found |
| `page_in_context` | ✅ | `test_agent_compress_history.cpp` |
| `set_episode_state` | 🔴 | none found |
| `page_in_history_auto` | ✅ | `test_agent_compress_history.cpp` |
| `calculate_current_tokens` | 🔴 | none directly |
| `save_active_state` / `load_active_state` | 🔴 | none found |
| `load_episode_index` | 🔴 | none found |
| `get_memory_index` | 🔴 | none found |
| `set_final_result` / `get_final_result` / `has_final_result` | ⚠️ | `test_report_final_result.cpp` |
| `get_task_description` / `set_task_description` | 🔴 | none directly |
| `set_exit_implicitly_on_idle` / `is_exit_implicitly_on_idle` | 🔴 | none directly |
| `set_notify_parent_on_completion` / `is_notify_parent_on_completion` | 🔴 | none directly |
| `set_suppress_parent_injection` / `is_suppress_parent_injection` | 🔴 | none directly |
| `get_animation_name` / `set_animation_name` | 🔴 | none directly |
| `get_last_activity_time_ms` / `update_last_activity_time` | 🔴 | none directly |
| `get_episode_index` | 🔴 | none directly |
| `inject_archived_episodes_summary` | 🔴 | none found |
| `compact_ephemeral_errors` | ⚠️ | via agentcli main.cpp; no unit test |
| `evaluate_auto_episode` | ✅ | `test_agent_compress_history.cpp:158`, `test_exit_summarization.cpp` |
| `evaluate_compaction` | 🔴 | none directly |
| `force_compaction` | 🔴 | none found |
| `get_conversation` | ✅ | many |
| `set_conversation` | ✅ | agentcli + some tests |
| `get_conversation_data` | 🔴 | none found |
| `agent_status_to_string` / `agent_status_to_name` | 🔴 | none directly |

Free functions in ai_agent.cpp: `parse_turns`, `get_last_50_words`, `is_episode_boundary_message` — all 🔴 (internal/anonymous, hard to reach directly).

**Key gap**: massive. The 30 existing agent tests cover mostly `create`, `inject_context`, `page_*`, conversation get/set, and a handful of flags. All the "session persistence" methods (save/load active state, episode index, memory index), stat/token accounting, interaction management, role/properties, subagent management, and status/activity helpers are essentially uncovered.

### 1.2 llm_client (src/agentlib/llm_client.h / llm_client.cpp — 147 lines)

| Method | Coverage | Where exercised |
|---|---|---|
| `llm_client` ctor | ⚠️ | `test_exit_summarization.cpp` includes header (via agent) but no direct unit test; agentcli main.cpp uses it |
| `~llm_client` | — | default dtor |
| `send_chat` | 🔴 | NO direct unit test (only via agentcli E2E replay) |
| `send_chat_stream` | 🔴 | NO direct unit test |
| `cancel` | 🔴 | NO direct unit test |
| `compact_response` | 🔴 | NO direct unit test |

**Key gap**: `llm_client` has *no* dedicated unit test despite being the central LLM-facing wrapper. Best approach: use `recording_transport`/`replay_transport` + the existing `mock_transport` pattern from `test_api_formatter.cpp` to drive `send_chat`, `send_chat_stream`, `cancel`, `compact_response`.

### 1.3 ai_model (src/agentlib/ai_model.h / ai_model.cpp — 346 lines)

| Method | Coverage | Where exercised |
|---|---|---|
| `ai_model` ctor | ✅ | `test_ai_model_scoring.cpp`, `test_model_server.cpp` |
| `get_id/get_name/get_purpose/get_url/get_api_key/get_api_type/get_max_context_tokens/...` | ✅ | `test_model_server.cpp` (get_url, get_api_key, get_api_type, server_id, from_download), scoring test |
| `calculate_score` | ✅ | `test_ai_model_scoring.cpp` |
| `calculate_and_record_cost` | ⚠️ | not directly asserted |
| `set_*/get_*` accessors | ✅ | `test_model_server.cpp` |

`ai_model_registry`:
| Method | Coverage |
|---|---|
| `get_instance` | ✅ |
| `register_model` | ✅ |
| `get_model` | ✅ |
| `get_default_model` | 🔴 mostly |
| `get_all_models` | ✅ (`test_ai_model_scoring`) |
| `remove_model` | ✅ |
| `update_model` | ⚠️ (not directly) |
| `load_models` / `save_models` | ✅ (`test_model_server.cpp`) |

**Gap**: `calculate_and_record_cost` token/cost accounting not directly asserted; `get_default_model` behavior not unit-tested.

### 1.4 command_registry (src/agentlib/command_registry.cpp — 434 lines)

| Method | Coverage |
|---|---|
| `get_instance` | ✅ |
| `register_command` / `unregister_command` | ⚠️ (via plugin tests) |
| `get_command` | ✅ (`test_clear_command`, `test_sysprompt_command`, `test_pluginloader`) |
| `get_command_names` | 🔴 |
| built-in command `.execute()` impls (quit/model/mcp/skills/compact/save/info/stats/memory/episode/pageout/pagein/help/clear/rescan/sysprompt) | ⚠️ only `clear` + `sysprompt` roughly covered; most `.execute()` bodies untested |

### 1.5 tool_registry + tool_validator (tool_registry.cpp — 374 lines; tool_validator.cpp — 38 lines)

| Method | Coverage |
|---|---|
| `get_instance` | ✅ |
| `register_validator` / `unregister_validator` | ✅ (`test_tool_infrastructure.cpp`, `test_tools.cpp`) |
| `register_tool_family` / `unregister_tool_family` | ✅ |
| `get_tool_family_reason` / `get_tool_family_guidance` | ⚠️ (indirect) |
| `has_tool_family` | ⚠️ |
| `get_tools_json` | ✅ |
| `get_active_tools` | ⚠️ |
| `get_all_registered_families` / `get_all_registered_validators` | 🔴 |
| `is_tool_silent` | ⚠️ |
| `prepare_tool` | ✅ |
| `execute_tool` | ✅ (dozens of tests) |
| `split_families` (anon) | 🔴 internal |
| `tool_validator::is_allowed_for_agent` | ✅ (`test_editor_tool_family.cpp`) |

Well covered overall — low priority.

### 1.6 httplib_transport (httplib_transport.cpp — 510 lines)

| Method | Coverage |
|---|---|
| `httplib_transport` ctor | ✅ |
| `cancel` | 🔴 |
| `post` | ✅ (incl. 503 retry logic) |
| `post_stream` | ✅ |
| `fetch_models_from_server` (free fn) | ✅ |
| `error_to_string` / `format_rich_diagnostics` | ⚠️ (proxy-diag assertions) |

Good coverage — low priority, but `cancel()` untested.

### 1.7 recording_transport / replay_transport (small)

| Method | Coverage |
|---|---|
| `recording_transport::post` | 🔴 |
| `recording_transport::post_stream` | 🔴 |
| `append_to_log` | 🔴 |
| `replay_transport` ctor | 🔴 |
| `replay_transport::post` | 🔴 |
| `replay_transport::post_stream` | 🔴 |
| `detect_api_type` | 🔴 |

**Gap**: Entirely untested. These are the E2E record/replay backbone (used by `agentcli_record`/`agentcli_replay`). A test here enables testing llm_client without network.

### 1.8 file_security_manager (file_security_manager.cpp — 227 lines)

| Method | Coverage |
|---|---|
| ctor / `set_working_directory` / `get_working_directory` | ✅ |
| `add_allowed_root` / `add_allowed_file` / `add_ignore_pattern` | ✅ |
| `load_ignore_file` | ✅ |
| `is_ignored` | ✅ |
| `validate_access` | ✅ |

Well covered — low priority.

### 1.9 virtual_file_system (virtual_file_system.cpp — 1012 lines)

| Area | Coverage |
|---|---|
| `virtual_file_system` core (mount/read/list/etc.) | ✅ `test_virtual_file_system.cpp` |
| `memory_vfs_provider` | ⚠️ |
| `github_vfs_provider` | ✅ `test_github_vfs.cpp`, `test_system_vfs.cpp` |
| `file_vfs_provider` | ✅ via many fs_* tests |
| `images_vfs_provider` | ✅ via image tests |
| `mmap_handle`, `count_lines`, curl cache helpers | 🔴 internal |

Good coverage — low priority.

### 1.10 skill_manager (skill_manager.cpp — 286 lines)

| Method | Coverage |
|---|---|
| `get_instance` / `get_vfs` / `get_skills` | ✅ |
| `register_skill` (all 3 overloads) | ✅ |
| `set_visibility` | ✅ |
| `initialize` | ✅ |
| `scan_and_mount` | ⚠️ |
| `format_skill_content` | ⚠️ |
| `unregister_skill` | ✅ |

Good — low priority.

### 1.11 subagent_manager (subagent_manager.cpp — 506 lines)

| Method | Coverage |
|---|---|
| `get_instance` / `initialize` / `rescan` | ✅ |
| `get_subagents` | ✅ |
| `get_a2a_subagents` / `is_subagent_a2a_exposed` / `set_subagent_a2a_exposed` | ⚠️ (a2a tests) |
| `find_subagent_by_name` | ✅ |
| `load_builtins` | ✅ (via initialize) |
| `load_from_string` | ⚠️ |
| `scan_directory` / `parse_subagent_file` | ✅ (via rescan tests) |
| `register_subagent` / `unregister_subagent` | ✅ |
| `generate_a2a_card_for_agent` | ✅ |
| `get_a2a_card` | ✅ |
| `parse_subagent_content` / `expand_path` (free) | ⚠️ (via rescan) |

Good — low priority.

### 1.12 compaction_engine (compaction_engine.cpp — 87 lines)

| Method | Coverage |
|---|---|
| `estimate_message_tokens` | ⚠️ (via tools test indirectly) |
| `plan_compaction` | ✅ `test_tools.cpp:204-217` |

Medium — `estimate_message_tokens` deserves direct assertions.

### 1.13 context_dnn (context_dnn.cpp — 357 lines)

| Method | Coverage |
|---|---|
| `get_instance` | ✅ |
| `load_weights` | ✅ |
| `compute_crc32` | ✅ |
| `tokenize` | ✅ |
| `pool_text` | ✅ |
| `predict_boundary` | ✅ |
| `evaluate_dense_layer` | ⚠️ internal |

Well covered — low priority.

### 1.14 copilot_manager (copilot_manager.cpp — 586 lines)

| Method | Coverage |
|---|---|
| `format_github_models_json` | ✅ `test_copilot_models.cpp` |
| `poll_device_authorization` throttling | ✅ |
| `set/get_github_access_token`, `is_authenticated`, `get_polling_interval` | 🔴 |
| `start_device_flow` | 🔴 |
| `get_copilot_token` | 🔴 |
| `fetch_and_register_github_models` / `query_and_write_github_models` | 🔴 (needs mocks/curl) |

**Gap**: Device-flow/token parts untested (network+user-interactive; harder). Formatting + throttling already covered.

### 1.15 model_server (model_server.cpp — 147 lines) — ✅ well covered by test_model_server.cpp

### 1.16 agent_animation (agent_animation.cpp — 127 lines) — ✅ test_agent_animation.cpp

### 1.17 interactions/* (base.cpp 367 lines, action, image_tool, terminal, tool_interaction, etc.)

| Method | Coverage |
|---|---|
| `agent_interaction::render/get_height/wrap_text/can_merge_with_previous` | ⚠️ (only via `test_agent_highlight.cpp` using llm_response; rendering not asserted) |
| `interaction_*::format_lines` for each subclass | 🔴 mostly |
| `interaction_tool_call/result/fs_grep_files` | 🔴 |
| `interaction_image_tool`, `interaction_terminal`, `interaction_action` | 🔴 |

**Gap**: `base.cpp`'s `wrap_text` (238 lines of logic!) and `render`, plus each subclass `format_lines`, are effectively untested. Medium-high priority given complexity of wrap_text.

### 1.18 data/* (conversation, episode, transaction, turns, turn_registry)

| Method | Coverage |
|---|---|
| `Conversation` core (create episode, add tx, seq, get_turns_since) | ✅ `test_conversation_turns.cpp` |
| `Conversation::serialize/deserialize` | ✅ |
| `Conversation::get_time_range`, `estimate_token_count`, `archive_current_episode`, world/connection state | 🔴 |
| `Episode::to_messages/insert_transaction/copy_from/estimate_token_count/get_time_range` | ⚠️ |
| `Transaction::to_markdown/estimate_token_count/get_time_range` | 🔴 |
| `model_response_turn::estimate_token_count/append_reasoning_content/to_markdown` | ⚠️ |
| `tool_execution_turn` (add_result/update_result_content) | ⚠️ |
| `TurnRegistry::deserialize` | ⚠️ |
| `system_turn/user_turn/error_turn` serialize/deserialize | ✅ (partial via convo tests) |

Medium — add serialization round-trip tests for each turn type, plus time-range + token estimation.

### 1.19 protocols/* (connection subclasses — 300-440 lines each)

| Method | Coverage |
|---|---|
| `connection_factory::create` | 🔴 (no direct dispatch test) |
| each connection `send_prompt` payload shaping | ✅ `test_api_formatter.cpp` (openai_completion, openai_response, gemini) |
| `claude_connection::send_prompt` | 🔴 not covered in test_api_formatter (only completion/response/gemini)! |
| stream delta parsing | ✅ (responses API) |
| `openai_response_connection::compact_response` | 🔴 |
| `normalize_history` for each protocol | ⚠️ (via send_prompt) |
| `process_user_content_*` | ⚠️ |

**Gap**: `claude_connection` payload shaping untested; `connection_factory::create` dispatch untested; `compact_response` untested.

---

## 2. Priority backlog (ordered)

1. **`llm_client`** — new `tests/unit/test_llm_client.cpp`
   - Cover: ctor, `send_chat`, `send_chat_stream`, `cancel`, `compact_response`.
   - Strategy: local `mock_transport` (like `test_api_formatter.cpp`) that emits canned stream chunks for `post_stream` and canned bodies for `post`; verify `send_chat` aggregates content/reasoning/tool_calls/usage/response_id; `send_chat_stream` delivers deltas; `cancel` closes connection; `compact_response` with unsupported → error message; `compact_response` with response connection → function dispatched.
   - Also exercise with `replay_transport` against a recorded traffic file (record via local LLM + `agentcli_record`).

2. **`ai_agent` state/persistence/session methods** — new `tests/unit/test_ai_agent_state.cpp` (or extend existing)
   - Cover (currently 🔴): `set_status`/`get_status` transitions, `get_current_tool`, `get_waiting_on_id`, `get_all_active_agents`/`find_agent_by_id` (registry), `get_tokens_tx/rx/cached/active`, `get_estimated_cost`, `get_compaction_segments`, `get_current_system_prompt`, `get_last_boundary_prob`, `get_last_inference_duration_ms`, `increment_stat`/`get_stats`, `get_interactions`/`add_interaction`, role/properties accessors, `get_allowed_write_file`/`set_allowed_write_file`, `is_mutation_possible`, task_description flags, animation name, activity time, `get_episode_index`, `get_conversation_data`, `agent_status_to_string/name`.
   - Many are simple getters/setters → cheap, high value for a first pass.

3. **`ai_agent` session persistence & episode machinery** — new `tests/unit/test_ai_agent_episodes.cpp`
   - Cover: `save_active_state`/`load_active_state`/`load_episode_index`/`get_memory_index`, `snapshot_episode`, `update_episode_hint`, `page_out_prior_context`, `set_episode_state`, `calculate_current_tokens`, `inject_archived_episodes_summary`, `force_compaction`/`evaluate_compaction`, `save_conversation`.
   - Note: several of these write to `~/.cache/turbostar/` based on agent name — MUST use `test_watchdog::isolate_home()` / scoped home so parallel runs don't collide.

4. **`ai_agent` lifecycle/subagent/admin** — extend existing tests (e.g. new `tests/unit/test_ai_agent_lifecycle.cpp`)
   - Cover: `replace_tool_result`, `cancel_current_task`, `close`, `remove_subagent`, `clear_conversation` behavior, `wait_until_idle` with status wires, `get_all_active_agents` cleanup after `close`.
   - `cancel_current_task` + `close` need a slow mock server (pattern from `test_exit_summarization.cpp`).

5. **`recording_transport` / `replay_transport`** — new `tests/unit/test_record_replay_transports.cpp`
   - Cover: replay ctor on valid/invalid/missing file; `post` sequential playback; `post_stream` chunk delivery; end-of-file → 404 + last_error; `detect_api_type` (responses/gemini/copilot/openai); recording_transport `post` forwards + `append_to_log` round-trip (write then replay-read).
   - Directly unblocks llm_client replay testing.

6. **`interactions/base.cpp` `wrap_text` + `render` + subclasses** — new `tests/unit/test_interactions_render.cpp`
   - Cover: `wrap_text` prefix/suffix/wrapping/color, `get_height`, `render` cache invalidation, `set_boxed`, `set_age`, `can_merge_with_previous`, and `format_lines` for action/image_tool/terminal/tool_interaction/reasoning/system_message/user_message/llm_response.
   - These are pure/self-contained → ideal unit targets. Emulates `test_agent_highlight.cpp` but asserts line text/colors.

7. **protocols gaps** — extend `tests/unit/test_api_formatter.cpp`
   - Add `claude_connection` payload test (roles mapping, tool defs, stream parsing).
   - Add `connection_factory::create` dispatch test for all 4 api_type values.
   - Add `openai_response_connection::compact_response` test (with mock transport capturing `/v1/responses/{id}/compact` request).

8. **`ai_model::calculate_and_record_cost` + `get_default_model`** — extend `test_ai_model_scoring.cpp`
   - Assert cost accumulation math for each `model_cost_type`, token-cache accounting, and `get_default_model` fallback behavior.

9. **`command_registry` remaining built-ins** — new `tests/unit/test_command_registry_commands.cpp`
   - Cover: get_command_names; execute each built-in command with a live `ai_agent` where applicable (compact, memory, episode, pageout/pagein, stats, save, info, help, rescan, quit, skills, mcp, model). Several require an agent+queue; assert no-crash + expected output markers.

10. **`compaction_engine::estimate_message_tokens`** — extend `test_tools.cpp` or new small test.

11. **`data/*` remaining methods** — extend `test_conversation_turns.cpp`
    - Round-trip serialize/deserialize for each turn type re error/tool/system/user; `estimate_token_count`; `get_time_range`; `archive_current_episode`; `TurnRegistry::deserialize` unknown-type handling; `transaction::to_markdown`.

12. **`httplib_transport::cancel`** — extend `test_httplib_transport.cpp`
    - Start a long-running handler + `post_stream` in a thread, call `cancel()` mid-request, verify the stream aborts.

## 3. Done

(none yet — starting fresh)

## 4. E2E / record-replay (deferred, secondary)

- We have a local LLM. Workflow (per `template_tool_test.cpp.example` + `src/agentcli/main.cpp`):
  - `./build/agentcli_record "prompt" tests/data/your_traffic.json` (compiled with `LLM_TRANSPORT_RECORD`) records HTTP traffic via `recording_transport`.
  - `./build/agentcli_replay` replays via `replay_transport` (`LLM_TRANSPORT_REPLAY`).
- Existing recorded traffic: `tests/data/*_traffic.json` (agent_create, crashdump, elf_tools, sqlite, test_management, todo).
- These are true end-to-end; the unit-test backlog above takes priority per the brief. Once the unit layer is solid, add replay coverage for `llm_client` + `ai_agent::submit_prompt` pipeline.

## 5. Notes / gotchas

- `get_conversation_data` returns `shared_ptr<Conversation>`; check it reflects injected context.
- Many `ai_agent` persistence methods touch `fs_utils::get_project_history_dir(name)` and `~/.cache/turbostar` — ALWAYS isolate HOME in these tests (watchdog does it automatically when called first in main; use scoped guard for custom names).
- `ai_agent::create` requires a registered model + `config_manager` default model id in some paths; follow `test_agent_compress_history.cpp` setup.
- `llm_client` is constructed with a `std::shared_ptr<llm_transport>`; for `send_chat` the `tool_registry` param is currently ignored (passed nullptr in impl) — good for simplification.
- `#define private public` trick is already used (test_exit_summarization) to access internals like `summary_queue_`; can be reused but prefer public API where possible.
- `test_api_formatter.cpp` has the canonical `mock_transport` class — copy/reuse the pattern for llm_client tests.
