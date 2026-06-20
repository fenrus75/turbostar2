# top level objective

Take code review capability of turbostar agentic system to the next level

# High level idea

- Create a set of tools to make code review a top level concept
- Create a pair of pre-configured agents to perform code review
- Adjust various infrastructure pieces around tool calls to do the agents well


# Code review tools

Code reviews and "code review items" become a top tier data structure and capability -- sort of parallel to todo items
We will persist code review data in a "review.json" in the per project data directory
(~/.cache/turbostar/projects/<...>/review.json)

## Datastructure for a code review item

A review item will need the following data, some optional

- ID   (project global and forever unique - so we need to store a counter in review.json for what the next ID will be and only ever increment) (filled in by turbostar)
- datestamp (unix time) of creation (filled in by turbostar) 
- git hash of project at time of creation (filled in by turbostar)

- summary
- filename  
- line number (optional)
- line content (filled in by turbostar at time of filing)
- state  (invalid, new, confirmed, disputed, stale, resolved, verified-fixed)
- severity (nit, low, medium, high, critical)
- description
- proposed_fix
- "resolved_in" git commit 

## Tools

Tools should be in a codereview tool group

- create_code_review_item
- update_code_review_item
- confirm_code_review_item (only available to verifier agent role) - either from new->confirmed or from fixed->verified_fixed
- resolve_code_review_item
- list_code_review_items   (high level overview) - can take filename or severity argument to filter
	normal agents only see current and stale
	verifier agents also see resolved
- get_code_review_item (full details)


this one is likely always available even outside the code review tool group:
- perform_code_review(file list, review_instructions, todo items, result_file, async)
    - creates a code review agent (agent 1)
    - when agent 1 completes, agent 2 (verification agent) spawns async
- security_review_with_agent(files, instructions, result_file)
    - spawns a dedicated security code review agent with the `:plugin:securityagent` tool family active


# Agent 1: Code review agent

(gets the codereview tool group auto-added)

Gets specific system instructions (see below), starts with a todo list passed in, and
the 'review_instructions' are the user prompt

operates basically similar to plan mode (read only, potentially except for result_file)

output: it does create_code_review_item on all issues it finds
 (which live update into the parent agent context for every create_code_review_item that it creates)

## System prompt proposal


You are a code review agent. Your task is to perform a detailed code review based on the user request only. Do not try to edit files to apply fixes.
Use the create_code_review_item tool to report any issue you find, including potential proposed solutions.

<if code reviews exist for the files passed in>
There are previous code review items on the files impacted - part of your task is to verify their correctness and relevance.
Use update_code_review_item to correct items when needed, or use complete_code_review_item() when the issue no longer applies.
Use get_code_review_item() to get full details on an item.
<markdown table of relevant previous code review items with the ID nr, file/line and super short summary>

<if todos were passed in:>
You are explicitly asked to resolve the following todo items and then mark these complete them using the <complete_todo> tool:
<table of todos>

<list of files impacted>
<if there was only 1 file, already provide the content to save a toolcall?>

<provide generic instructions on VFS, coding style from .clangformat, and GEMINI.md>


# Agent 2: Review verification agent

Goals:
- verify if reviews created by agent 1 are actually correct (transitioning from `new` to `confirmed` or `disputed`)
- verify if resolved issues are indeed fixed and mark them as `verified-fixed` (transitioning from `resolved` to `verified-fixed`)

Execution & Context:
- Spawns automatically and asynchronously once Agent 1 completes.
- Supplied with the reported code review findings, current target file contents, and the workspace git diff (when verifying resolved fixes).
- Can use a lower cost model (say claude haiku).

# Editor interaction

- code review items will be treated like compiler warnings in the highlighter. Severities nit/low will be highlighted as warnings (pair 28, black-on-yellow), while medium/high/critical will be highlighted as errors (pair 27, white-on-red).
- When the cursor is on a line with a code review item, its summary is shown in the status bar. The status bar diagnostic message is interactive/clickable: clicking it directly opens the Code Review Window focused on the corresponding item details.
- **Line Tracking and Stale Threshold**: While files are actively open, edit actions (typing, insertions, deletions) shift review item line numbers dynamically in memory. When files are loaded or saved, git diff mapping is used to update line numbers. If the code shifts too far or the target line content is significantly modified or deleted, tracking is halted and the item's state transitions to `stale`. Stale items remain visible in the Code Review Window list but their active editor highlights are removed. They are meant to be updated or cleared by subsequent agent runs.
- **Code Review Window**: A dedicated editor screen (`code_review_window`, similar to the crashdump window) will list all code review items. 
  - Accessible via the "Tools" -> "View Code Reviews" menu option (no global keyboard shortcuts are mapped to preserve key combination namespace).
  - Uses a split-screen panel layout: the left side displays a scrollable list of items (ID, severity, state, short summary), and the right side displays the detailed view (description, proposed fix, comments, and options/hotkeys to perform state transitions).
  - The user can view full details of any item.
  - The user can manually edit items, add comments, and change states (e.g. resolve them, move `disputed` or `stale` items to `invalid`, or override verification).
- **Active Agents Integration**: Background code review runs are displayed in the active agents list until they complete.
- **Persistence**: Managed by a dedicated C++ `codereview_manager` singleton class that maintains the in-memory state of active review items, processes thread-safe reads and writes to `review.json` under `fs_utils::get_project_cache_root()`, and coordinates UI refreshes on update.
- **Model Configuration (Task-to-Model Mapping)**: The model used by Agent 1 and Agent 2 will be configured via a new "Task-to-Model Mapping" configuration dialog (accessible from Options/Preferences). This mapping defines default models for "Code Reviewer" (Agent 1) and "Code Review Verifier" (Agent 2) tasks.

# normal system prompt
```
*** CRITICAL DIRECTIVE: CODE REVIEWS ***
When asked to perform a code review on files, do NOT manually read/analyze the files in your own context.
Instead, you MUST use the `perform_code_review` tool. This tool automatically runs a read-only specialized Code Review Agent
to analyze the code and generate a Markdown summary.
- **File Slicing**: Do not review more than 10 files or 1,500 lines of code in a single subagent call. Group larger lists logically
  and invoke `perform_code_review` separately for each group.
- **Instructions & Tasks**: Provide overall context in `instructions` and supply a checklist of specific review items in the `tasks` vector.
- **Post-Review**: Call `list_code_review_items` to get a concise summary table of active findings, and `get_code_review_item` to retrieve details.
- **Resolution**: Use `resolve_code_review_item` when issues are addressed or ruled out.
```

## Hybrid File Splitting Heuristics
To handle large file lists efficiently:
- The system instructs the agent to slice the review files into logical groups.
- The system enforces a guardrail limit (e.g., max 10 files or 1500 lines of code) per subagent. If the agent-suggested list exceeds this limit, the system automatically partitions the list into smaller runs.
- Every subagent concurrently writes its findings to the project-level `review.json` database via `create_code_review_item`.

# Role-based Tool Filtering and Validation
- The `tool_validator` / registry is extended to support role-based validation.
- When a subagent is defined or spawned, it is assigned a specific role (e.g., `reviewer`, `verifier`, `developer`).
- The registry automatically filters out tools that do not match the agent's role and programmatically rejects execution if a tool is called by an unauthorized role (e.g., `confirm_code_review_item` can only be invoked by `verifier`).

# Other related ideas
- scan meson.build for a set of known dependencies and provide specific github:// locations for these
- some tools may behave differently for different roles, example fs_read_lines may provide more extensive diagnostic information to a code review agent (like code complexity metrics)
