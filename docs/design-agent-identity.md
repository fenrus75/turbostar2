# LLM Agent Identity, Roles, Modes, and Capabilities

This document records the current (status quo) architecture for managing agent identities, roles, execution modes, tool groups, and filesystem capabilities in Turbostar. It serves as a clear mapping of the current implementation and a starting point for a planned refactoring to enable plugin-based identities and cleaner separation of concerns.

---

## 1. Architectural Concepts

The concepts of what an agent "is" (identity/role) and what it "can do" (modes/capabilities/tool access) have been unified under a single structure, `agent_properties` defined in [agent_properties.h](file:///home/arjan/git/turbostar2/src/agentlib/agent_properties.h):

```cpp
struct agent_properties {
	agent_role role = agent_role::developer;
	bool read_only = false;
	std::vector<std::string> active_families;
};
```

This struct is stored directly in `ai_agent::properties_` (protected by `properties_mutex_`) and passed directly through the client and connection layers down to the `tool_registry`:

```
[ ai_agent ] ──(properties_)──> [ llm_client ] ──(properties)──> [ Connection ] ──(properties)──> [ tool_registry ]
```

### A. Agent Role (`agent_role`)
The agent's role is represented by the `agent_role` enum defined in [agent_role.h](file:///home/arjan/git/turbostar2/src/agentlib/agent_role.h):
* `developer`: The primary autonomous developer agent. Has full read-write capabilities on the workspace root.
* `reviewer`: A specialized code review agent.
* `verifier`: A verification agent that reviews the code reviewer's findings.
* `summarizer`: A backend summarization agent (has no tools).

The role is now used primarily for role-based tool restrictions via `tool_validator::is_allowed_for_agent(properties)`.

### B. Decoupled Read-Only Capability
The `read_only` boolean indicates if the agent is prohibited from executing state-modifying tools. This capability is decoupled from the role:
* **Sandbox Enforcement:** In [ai_agent.cpp](file:///home/arjan/git/turbostar2/src/agentlib/ai_agent.cpp), workspace write permissions are granted based on `!self->is_read_only()` rather than checking if the role is `developer`.
* **Tool Validation:** The `tool_registry` blocks non-pure tool execution if `ctx.properties.read_only` is true.
* **Plan Mode:** When entering Plan Mode, `properties_.read_only` is set to `true`, and when exiting Plan Mode, it is restored back to the default for that agent's role.

### C. Active Tool Families
The list of active tool groups is stored directly in `properties.active_families` instead of a separate vector:
* **Activation:** Additional families (such as `x86`, custom MCP servers) are activated dynamically using the `activate_tool_family` tool, which appends the family name to `properties.active_families`.
* **System Prompt Injection:** Every time a family is activated, the agent rebuilds its system prompt by appending the list of active families and a Markdown table of inactive families and instructions on when the agent should activate them.
* **Registry Filtering:** The `Connection` requests tool schemas from the `tool_registry` by passing the `properties` struct. The registry filters out tools whose family is not present in `properties.active_families`.

---

## 2. Code Execution Flow

The following flows show how these parameters interact during tool validation and code review:

### Tool Validation Flow
When a tool is requested by the model:
1. `tool_registry::prepare_tool` is called.
2. **Family Check:** Checks if the tool's family is active (`ctx.is_family_active(family)`). If not, aborts.
3. **Agent Check:** Checks if the tool is allowed for the agent's properties (`validator->is_allowed_for_agent(ctx.properties)`). If not, aborts.
4. **Read-Only Check:** If `ctx.properties.read_only` is true, checks if the tool is pure (`validator->is_pure()`). If not, aborts.
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
