#include "a2a_connect_server.h"
#include "fs_utils.h"
#include <format>
#include "a2a/a2a_client.h"
#include "a2a/a2a_server_manager.h"

namespace tools
{

a2a_connect_server_tool::a2a_connect_server_tool(a2a_connect_server_args args) : args_(std::move(args))
{
}

bool a2a_connect_server_tool::validate_runtime(const agentlib::tool_context &, std::string &out_error) const
{
	if (args_.name.empty()) {
		out_error = "Execution Error: Server name cannot be empty.";
		return false;
	}
	if (args_.url.empty()) {
		out_error = "Execution Error: Server URL cannot be empty.";
		return false;
	}
	return true;
}

std::string a2a_connect_server_tool::execute(agentlib::tool_context &)
{
	std::string err;
	auto card = a2a::a2a_client::get_instance().fetch_agent_card(args_.url, err, args_.auth_token);
	if (!card.has_value()) {
		return std::format("Error connecting to A2A server '{}' at '{}': {}", args_.name, args_.url, err);
	}

	a2a::a2a_server_config cfg;
	cfg.name = args_.name;
	cfg.url = args_.url;
	cfg.auth_token = args_.auth_token;
	cfg.tier = args_.persistent ? a2a::a2a_server_tier::project_local : a2a::a2a_server_tier::ephemeral_runtime;

	a2a::a2a_server_manager::get_instance().add_server(cfg);
	if (args_.persistent) {
		a2a::a2a_server_manager::get_instance().save_project_servers();
	}

	std::string skills_summary;
	if (!card->skills.empty()) {
		skills_summary = std::format("\nExposed agent skills/profiles: {}", nlohmann::json(card->skills).dump());
	}

	std::string res = std::format("Successfully connected to A2A server '{}' ({}) at '{}'.{}", args_.name, card->name, args_.url, skills_summary);
	return fs_utils::wrap_prompt_untrusted_data_tag("a2a_server_connection_result", res);
}

} // namespace tools
