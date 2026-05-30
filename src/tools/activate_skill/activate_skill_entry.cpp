#include <sstream>
#include "../../agentlib/ai_agent.h"
#include "activate_skill.h"

namespace tools
{

activate_skill_tool::activate_skill_tool(activate_skill_args args) : args_(std::move(args))
{
}

bool activate_skill_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true; // Name validation was already done in security phase
}

std::string activate_skill_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.active_agent) {
		ctx.active_agent->add_active_skill(args_.name);
	}

	auto vfs = ctx.fs_security.get_vfs();
	if (!vfs) {
		return "Error: Virtual File System not initialized.";
	}

	std::string base_uri = args_.target_skill.uri;
	if (!base_uri.empty() && base_uri.back() != '/') {
		base_uri += '/';
	}

	std::string skill_md_uri = base_uri + "SKILL.md";
	auto view_opt = vfs->read_file(skill_md_uri);

	std::string instructions;
	if (view_opt) {
		instructions = std::string(view_opt.value());
	} else {
		instructions = "Error: SKILL.md not found in skill root.";
	}

	std::stringstream ss;
	ss << "<skill_content name=\"" << args_.name << "\">\n";
	ss << instructions << "\n\n";
	ss << "Skill directory: `" << base_uri << "`\n";
	ss << "Relative paths in this skill are relative to the skill directory.\n\n";
	ss << "<skill_resources>\n";

	auto entries = vfs->list_directory(base_uri);
	for (const auto &entry : entries) {
		// Guard against malformed URIs
		if (entry.uri.rfind(base_uri, 0) != 0)
			continue;

		// Strip the base URI prefix to just show the relative path
		std::string filename = entry.uri.substr(base_uri.length());
		if (filename.empty())
			continue;

		// Skip directory entries themselves from the <file> list, just list actual files
		if (entry.type == 'D')
			continue;

		ss << "  <file>" << filename << "</file>\n";
	}

	ss << "</skill_resources>\n";
	ss << "</skill_content>";

	return ss.str();
}

} // namespace tools
