#include "list_subagents.h"
#include "agentlib/subagent_manager.h"
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
	const auto &subagents = agentlib::subagent_manager::get_instance().get_subagents();
	if (subagents.empty()) {
		return "No subagents available.";
	}

	std::stringstream ss;
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
	return ss.str();
}

} // namespace tools
