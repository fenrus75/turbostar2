#include "../../config_manager.h"
#include "../../fs_utils.h"
#include "agent_set_application_binary.h"

namespace tools
{

bool agent_set_application_binary_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string agent_set_application_binary_tool::execute(agentlib::tool_context &ctx)
{
	config_manager::get_instance().set_main_executable(args_.path);

	// Save configuration to project cache root or fallback to global config
	std::string cache_root = fs_utils::get_project_cache_root();
	if (!cache_root.empty()) {
		config_manager::get_instance().save_project(cache_root);
	} else {
		config_manager::get_instance().save_global();
	}

	set_success(ctx, "Main executable set to: " + args_.path);
	return "Main application binary path successfully set to: " + args_.path;
}

} // namespace tools
