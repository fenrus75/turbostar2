# LLM Agent Identity, Roles, Modes, and Capabilities

This document records the current (status quo) architecture for managing agent identities, roles, execution modes, tool groups, and filesystem capabilities in Turbostar. It serves as a clear mapping of the current implementation and a starting point for a planned refactoring to enable plugin-based identities and cleaner separation of concerns.

---

## 1. Architectural Concepts (The Status Quo)

Currently, the concepts of what an agent "is" (identity/role) and what it "can do" (modes/capabilities/tool access) are tightly coupled and scattered across multiple layers of the codebase:

```
[ ai_agent ] ──(role_)──> [ llm_client ] ──(string identity)──> [ Connection ] ──(role_ enum)──> [ tool_registry ]
```

### A. Agent Role (`agent_role`)
The agent's role is represented by the `agent_role` enum defined in [agent_role.h](file:///home/arjan/git/turbostar2/src/agentlib/agent_role.h):
* `developer`: The primary autonomous developer agent. Has full read-write capabilities on the workspace root.
* `reviewer`: A specialized code review agent. Defaulted to read-only except for a specific review output file.
* `verifier`: A verification agent that reviews the code reviewer's findings.
* `summarizer`: A backend summarization agent (has no tools).

The role is stored directly in `ai_agent::role_` and determines:
1. **Tool Access:** Tools can restrict their execution to specific roles via `tool_validator::is_allowed_for_role(role)`.
2. **File Permissions:** Inside the main agent loop, if `role_ == agent_role::developer`, the filesystem security context is granted write access to the entire workspace. Otherwise, it is read-only by default.

### B. Agent Identity
"Identity" refers to the semantic personality of the agent as understood by the LLM client and connection layers. 
* Currently, the `ai_agent`'s `agent_role` enum is translated into a string (`"developer"`, `"reviewer"`, `"verifier"`, `"summarizer"`) by the `llm_client`.
* This string is passed to the `Connection` subclasses (e.g. `openai_completion_connection`), which immediately convert it **back** to the `agent_role` enum in order to query the `tool_registry`.
* This double-conversion prevents extensible or custom plugin-defined identities.

### C. Agent Mode (e.g., Plan Mode)
"Plan Mode" is a specialized execution state where the agent is restricted to reading the codebase and outputting a plan of action.
* **State Flag:** Managed via `is_planning_` (`std::atomic<bool>`) in `ai_agent`.
* **Transitions:** Triggered by executing the `enter_plan_mode` tool and ended using the `exit_plan_mode` tool.
* **Constraints:** When `is_planning()` is true, the `tool_registry` blocks the execution of all state-modifying tools unless:
  1. The tool is explicitly `exit_plan_mode`.
  2. The tool overrides `is_allowed_in_plan_mode(args, ctx)` to return `true` (e.g. `fs_write_file` or `fs_replace_lines` check that the target path matches `ctx.active_agent->get_plan_file()`).

### D. Agent Capabilities (File Permissions)
Capabilities define the specific sandboxed actions (specifically file read/write permissions) allowed for an agent instance:
* **Read Access:** The agent is always granted read access to the entire workspace directory root.
* **Write Access:**
  * Developer role: Write access is granted to the entire workspace root.
  * Non-developer roles: Read-only by default, but a single write-exception can be granted via `allowed_write_file_` (configured via `ai_agent::set_allowed_write_file`).
* **Enforcement:** Enforced at the beginning of the `ai_agent` thread loop when configuring the `tool_context::fs_security` context:
  ```cpp
  ctx.fs_security.add_allowed_root(workspace_root, access_type::read);
  std::string allowed_write = self->get_allowed_write_file();
  if (!allowed_write.empty()) {
      ctx.fs_security.add_allowed_file(workspace_root / allowed_write, access_type::write);
  }
  if (self->get_role() == agent_role::developer) {
      ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
  }
  ```

### E. Tool Groups / Families
Tools are grouped into "families" (defined by `tool_validator::get_family()`).
* **Active Families:** The agent tracks active tool groups via `active_tool_families_` (always defaults to `{"base"}`).
* **Activation:** Additional families (such as `x86`, custom MCP servers) are activated dynamically using the `activate_tool_family` tool.
* **System Prompt Injection:** Every time a family is activated, the agent rebuilds its system prompt by appending the list of active families and a Markdown table of inactive families and instructions on when the agent should activate them.
* **Registry Filtering:** The `Connection` requests tool schemas from the `tool_registry` by passing the `active_families` vector. The registry excludes tools belonging to inactive families.

---

## 2. Code Execution Flow

The following flows show how these parameters interact during tool validation and code review:

### Tool Validation Flow
When a tool is requested by the model:
1. `tool_registry::prepare_tool` is called.
2. **Family Check:** Checks if the tool's family is active (`ctx.is_family_active(family)`). If not, aborts.
3. **Role Check:** Checks if the tool is allowed for the agent's role (`validator->is_allowed_for_role(role)`). If not, aborts.
4. **Read-Only Check:** If the agent is read-only, checks if the tool is pure (`validator->is_pure()`). If not, aborts.
5. **Plan Mode Check:** If the agent is in Plan Mode:
   * Rejects if tool name is not `exit_plan_mode` and `validator->is_allowed_in_plan_mode(args, ctx)` returns `false`.
   * Write tools validate that the file target matches the configured `plan_file_`.
6. **Execution:** If all checks pass, the tool executes within the configured `fs_security` restrictions.

### Code Reviewer Subagent Example
The `perform_code_review` tool showcases how these concepts intertwine:
1. The developer agent calls `perform_code_review`.
2. The tool spawns a new reviewer subagent via `parent->spawn_subagent(name)`.
3. The reviewer agent's role is set to `agent_role::reviewer` (making it read-only by default).
4. The reviewer agent's allowed write path is set: `reviewer_agent->set_allowed_write_file(args.result_file)`. This restricts the reviewer subagent to writing only to the code review output report.
5. The reviewer subagent executes, inspects files, writes its report, and ends.

---

## 3. Structural Limitations in the Current Design

* **Rigid Role Enum:** Creating a new agent identity requires adding it to the `agent_role` enum in `agent_role.h` and adding conversion switch-cases across multiple source files.
* **Identity Mapping Round-trip:** The translation of `role` -> `identity string` -> `role` between `ai_agent`, `llm_client`, and `Connection` is redundant and prevents decoupling.
* **Single Allowed Write File Exception:** `allowed_write_file_` is a single string member, limiting read-only roles to a single file modification context.
* **Scattered Security Policy:** Constraints for Plan Mode, read-only status, roles, and filesystem safety are checked in an ad-hoc manner across `tool_registry`, `tool_validator`, and individual validator implementations.
