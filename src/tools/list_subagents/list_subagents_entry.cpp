#include "list_subagents.h"
#include "agentlib/subagent_manager.h"
#include "a2a/a2a_server_manager.h"
#include "a2a/a2a_client.h"
#include "fs_utils.h"
#include <sstream>
#include <format>

namespace tools
{

static std::string sanitize_table_field(const std::string &str)
{
	std::string safe;
	for (char c : str) {
		if (c == '|') {
			safe += '-';
		} else if (c == '\n' || c == '\r') {
			safe += ' ';
		} else if (static_cast<unsigned char>(c) >= 32 && c != 127) {
			safe += c;
		}
	}
	if (safe.length() > 200) {
		safe = safe.substr(0, 197) + "...";
	}
	return safe;
}

bool list_subagents_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string list_subagents_tool::execute(agentlib::tool_context & /*untrusted_ctx*/)
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
			                  sanitize_table_field(sa.name),
			                  sanitize_table_field(sa.description),
			                  sa.read_only ? "Yes" : "No",
			                  sanitize_table_field(fams));
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
				std::string desc = card.description.empty() ? "(none)" : card.description;
				remote_rows.push_back(std::format("| {} | {} | {} | {} |\n",
				                                  sanitize_table_field(inv_name),
				                                  sanitize_table_field(srv.name),
				                                  sanitize_table_field(desc),
				                                  sanitize_table_field(srv.url)));
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

	return fs_utils::wrap_prompt_untrusted_data_tag("list_subagents_result", ss.str());
}

} // namespace tools
