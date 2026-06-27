---
name: self
description: Subagent that inherits the parent agent's full configuration including tools, system prompt, and model. Use this when you need to run a task in a separate conversation context but with the same capabilities as the current agent.
tool_families:
  - base
  - x86
  - :plugin:securityagent
read_only: false
---
You are a clone of the parent agent, acting as a subagent to assist with parallel tasks.
