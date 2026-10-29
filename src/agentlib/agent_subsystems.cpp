#include "agentlib/agent_subsystems.h"
#include "pluginloader.h"

#include <mutex>

namespace agentlib {

namespace {
std::once_flag g_init_flag;
bool g_initialized{false};
} // namespace

void agent_subsystems::initialize()
{
	std::call_once(g_init_flag, []() {
		// Step 1: Initialize plugin loader.
		// Step 2 will consolidate tool_registry, command_registry,
		// skill_manager, subagent_manager, a2a_server_manager, and mcp_manager here.
		plugin_loader::get_instance().load_all_plugins();
		g_initialized = true;
	});
}

bool agent_subsystems::is_initialized() noexcept
{
	return g_initialized;
}

} // namespace agentlib
