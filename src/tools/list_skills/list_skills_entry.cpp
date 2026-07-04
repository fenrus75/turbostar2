#include <sstream>
#include "../../agentlib/skill_manager.h"
#include "list_skills.h"

namespace tools
{

bool list_skills_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string list_skills_tool::execute(agentlib::tool_context &ctx)
{
	auto &skills = agentlib::skill_manager::get_instance().get_skills();

	std::stringstream ss;
	ss << "| Skill Name | URI | Description |\n";
	ss << "| ---------- | --- | ----------- |\n";

	int visible_count = 0;
	for (const auto &s : skills) {
		if (s.visible) {
			ss << "| " << s.name << " | " << s.uri << " | " << s.description << " |\n";
			visible_count++;
		}
	}

	if (visible_count == 0) {
		set_success(ctx, "0 skills");
		return "No skills are currently available.";
	}

	set_success(ctx, std::to_string(visible_count) + " skills");
	return ss.str();
}

} // namespace tools
