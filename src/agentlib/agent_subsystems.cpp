#include "agentlib/agent_subsystems.h"
#include "a2a/a2a_server_manager.h"
#include "agentlib/command_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/subagent_manager.h"
#include "agentlib/tool_registry.h"
#include "mcp/mcp_manager.h"
#include "pluginloader.h"
#include "project_manager.h"

#include <mutex>
#include <string>

namespace agentlib {

namespace {
std::once_flag g_init_flag;
bool g_initialized{false};
} // namespace

void agent_subsystems::initialize(bool server_mode)
{
	std::call_once(g_init_flag, [server_mode]() {
		plugin_loader::get_instance().load_all_plugins();
		(void)tool_registry::get_instance();
		(void)command_registry::get_instance();
		skill_manager::get_instance().initialize();
		subagent_manager::get_instance().initialize();

		if (!server_mode) {
			a2a::a2a_server_manager::get_instance().initialize();
			std::string project_root = project_manager::get_instance().get_project_root();
			mcp_manager::get_instance().start_async(project_root);
		}
		g_initialized = true;
	});
}

bool agent_subsystems::is_initialized() noexcept
{
	return g_initialized;
}

} // namespace agentlib
