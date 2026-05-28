# Implementation Plan: Recover Pending Tool Calls on Session Restart

// TS_FORMAT_START
## Overview
When the editor session is closed, killed, or restarted while the agent is executing a tool call, the saved active state contains an assistant message with `tool_calls` but no corresponding `tool` response. When resumed, the LLM will reject new user inputs because it expects a response to those pending tool calls. 
// TS_FORMAT_END

This plan details how we will detect these pending tool calls at load time and append abort messages to safely satisfy the LLM client API.

---

## 1. Code Changes

### Target File: `src/agentlib/ai_agent.cpp`

1. **Include Headers**: Ensure `<map>` and `<set>` are included at the top of the file to support tracking structures.
2. **Modify `load_active_state`**:
   - Locate the block where `conversation_` is successfully loaded and populated from `active_state.json`.
   - Before returning `true`, run a single pass over the loaded `conversation_` vector.
   - Track all tool call IDs declared in `assistant` messages and erase them once a matching `tool` response message is seen.
   - For any IDs remaining in the pending set, create and append a new message to `conversation_` with:
     - `role = "tool"`
     - `tool_call_id = <pending_id>`
     - `name = <tool_name>`
     - `content = "Tool execution aborted: Editor session was restarted before completion."`
   - Log the recovery action to the `event_logger`.

---

## 2. Unit Testing Plan

### Target File: `tests/unit/test_pop_todo.cpp` (or a dedicated unit test block)

We will append a unit test to verify this recovery. Since `test_pop_todo.cpp` already links with the full `ai_agent` library, we can add a test function `test_tool_call_recovery()` there:

1. **Setup State File**: 
   - Write a mock `active_state.json` directly to a temporary directory.
   - The JSON will contain a conversation with:
     - A system message.
     - A user message.
     - An assistant message with a tool call (e.g., `id: "call_abc"`, `name: "fs_write_file"`).
     - No corresponding tool response.
2. **Load & Verify**:
   - Instantiate `ai_agent` with a temporary `HOME` environment variable pointing to the mock directory.
   - Call `load_active_state()`.
   - Assert that `load_active_state()` returns `true`.
   - Retrieve the conversation using `get_conversation()`.
   - Assert that the conversation has been appended with a new message:
     - `role == "tool"`
     - `tool_call_id == "call_abc"`
     - `name == "fs_write_file"`
     - contains `"Tool execution aborted"` in its content.
3. **Clean up**:
   - Remove the temporary directory and files.

---

## 3. Execution Steps

1. Confirm approval of this plan.
2. Apply the C++ changes to [ai_agent.cpp](file:///home/arjanvandeven/git/turbostar/src/agentlib/ai_agent.cpp).
3. Add the unit test to [test_pop_todo.cpp](file:///home/arjanvandeven/git/turbostar/tests/unit/test_pop_todo.cpp).
4. Run `ninja -j2 -C build` to compile.
5. Run the unit tests via `meson test -C build` to verify the fix works.
6. Commit changes.
