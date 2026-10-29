#pragma once

namespace agentlib {

/**
 * agent_subsystems coordinates the deferred one-time initialization of all
 * heavy AI agent subsystems, background services, tool registries, and plugins.
 *
 * It ensures that standard editor startup ("just editing a file") stays lightweight,
 * fast, and avoids spawning child processes or loading dozens of dynamic libraries
 * until an agent feature, MCP server, or plugin-dependent UI dialog is requested.
 */
class agent_subsystems {
public:
	agent_subsystems() = delete;
	~agent_subsystems() = delete;

	/**
	 * Initializes all agent-side subsystems exactly once in a thread-safe manner.
	 * Subsequent calls return immediately.
	 */
	static void initialize();

	/**
	 * Returns true if agent subsystems have already been initialized.
	 */
	[[nodiscard]] static bool is_initialized() noexcept;
};

} // namespace agentlib
