#include "list_subagents.h"
#include "agentlib/subagent_manager.h"
#include "a2a/a2a_server_manager.h"
#include "a2a/a2a_client.h"
#include <sstream>
#include <format>

namespace tools
{

bool list_subagents_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string list_subagents_tool::execute(agentlib::tool_context &/*ctx*/)
{
	std::stringstream ss;
	const auto &subagents = agentlib::subagent_manager::get_instance().get_subagents();

	ss << "### Local Subagents\n";
	if (subagents.empty()) {
		ss << "No local subagents available.\n";
	} else {
		ss << "| Name | Description | Read-Only | Families |\n";
		ss << "| --- | --- | --- | --- |\n";
		for (const auto &sa : subagents) {
			std::string fams;
			if (sa.tool_families.empty()) {
				fams = "(none)";
			} else {
				for (size_t i = 0; i < sa.tool_families.size(); ++i) {
					if (i > 0) fams += ", ";
					fams += sa.tool_families[i];
				}
			}
			ss << std::format("| {} | {} | {} | {} |\n",
			                  sa.name,
			                  sa.description,
			                  sa.read_only ? "Yes" : "No",
			                  fams);
		}
	}

	auto servers = a2a::a2a_server_manager::get_instance().get_all_servers();
	if (!servers.empty()) {
		ss << "\n### Remote A2A Subagents\n";
		std::vector<std::string> remote_rows;
		for (const auto &srv : servers) {
			std::string err;
			auto cards = a2a::a2a_client::get_instance().fetch_all_cards(srv.url, err, srv.auth_token);
			for (const auto &card : cards) {
				std::string inv_name = std::format("{}:{}", srv.name, card.name);
				remote_rows.push_back(std::format("| {} | {} | {} | {} |\n",
				                                  inv_name,
				                                  srv.name,
				                                  card.description.empty() ? "(none)" : card.description,
				                                  srv.url));
			}
		}
		if (remote_rows.empty()) {
			ss << "No remote subagents reachable from configured A2A servers.\n";
		} else {
			ss << "| Invocation Name | Server Handle | Description | Base URL |\n";
			ss << "| --- | --- | --- | --- |\n";
			for (const auto &row : remote_rows) {
				ss << row;
			}
		}
	}

	return ss.str();
}

} // namespace tools
