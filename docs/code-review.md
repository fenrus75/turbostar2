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
- when loading from disk and at various times (compile/save), line-shift tracking (via git diff/patch mapping) is used to update line numbers dynamically when surrounding code shifts. If the actual target line content is changed or deleted, the item is marked as `stale`.
- **Code Review Window**: A dedicated editor screen (`code_review_window`, similar to the crashdump window) will list all code review items. 
  - The user can view full details of any item.
  - The user can manually edit items, add comments, and change states (e.g. resolve them, move `disputed` or `stale` items to `invalid`, or override verification).
- **Active Agents Integration**: Background code review runs are displayed in the active agents list until they complete.
- **Persistence**: Managed by a dedicated C++ `codereview_manager` singleton class that maintains the in-memory state of active review items, processes thread-safe reads and writes to `review.json` under `fs_utils::get_project_cache_root()`, and coordinates UI refreshes on update.

# normal system prompt
```
Prefer to use the perform_code_review() tool for code reviews; this tool will spawn a dedicated code review agent. You can pass overall instructions as well as a detailed todo list to this agent.
Consider limiting the number of files per code review agent to limit context size while keeping related files together.
```

## Hybrid File Splitting Heuristics
To handle large file lists efficiently:
- The system instructs the agent to slice the review files into logical groups.
- The system enforces a guardrail limit (e.g., max 10 files or 1500 lines of code) per subagent. If the agent-suggested list exceeds this limit, the system automatically partitions the list into smaller runs.
- Every subagent concurrently writes its findings to the project-level `review.json` database via `create_code_review_item`.

# Other related ideas

- scan meson.build for a set of known dependencies and provide specific github:// locations for these
- read only agents likely should not even get to see tools they cannot use, we need a lot better tool filter capabilties based on roles
- some tools may behave differently for different roles, example fs_read_lines may provide more extensive diagnostic information to a code review agent 
  (like code complexity metrics)
