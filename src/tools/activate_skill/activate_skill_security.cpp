#include <algorithm>
#include <nlohmann/json.hpp>
#include "../../agentlib/skill_manager.h"
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "activate_skill.h"

namespace tools
{

struct activate_skill_raw_args {
	std::string name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(activate_skill_raw_args, name);

class activate_skill_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "activate_skill";
	}
	std::string get_description() const override
	{
		std::string base_desc =
		    "Activates a specialized agent skill by name. Returns the skill's instructions wrapped in <skill_content> tags. These "
		    "provide specialized guidance for the current task. Use this when you identify a task that matches a skill's "
		    "description. ONLY use names exactly as they appear in the <available_skills> section.";

		auto &skills = agentlib::skill_manager::get_instance().get_skills();
		if (skills.empty()) {
			return base_desc;
		}

		nlohmann::json skills_arr = nlohmann::json::array();
		for (const auto &s : skills) {
			skills_arr.push_back({{"name", s.name}, {"description", s.description}, {"location", s.uri}});
		}

		return base_desc + "\n\n<available_skills>\n" + skills_arr.dump() + "\n</available_skills>";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"name", {{"type", "string"}, {"description", "The name of the skill to activate."}}}}},
			{"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			activate_skill_raw_args parsed = raw_json.get<activate_skill_raw_args>();
			if (parsed.name.empty()) {
				out_error = "Skill name cannot be empty.";
				return false;
			}

			args_.name = parsed.name;

			// Check if skill exists and stash it
			bool found = false;
			for (const auto &s : agentlib::skill_manager::get_instance().get_skills()) {
				if (s.name == args_.name) {
					args_.target_skill = s;
					found = true;
					break;
				}
			}

			if (!found) {
				out_error = "Skill '" + args_.name + "' not found. Check available skills.";
				return false;
			}

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<activate_skill_tool>(args_);
	}

      private:
	mutable activate_skill_args args_;
};

REGISTER_TOOL(activate_skill_validator)

} // namespace tools