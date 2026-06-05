#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "activate_tool_family.h"

namespace tools
{

struct activate_tool_family_raw_args {
	std::string name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(activate_tool_family_raw_args, name);

class activate_tool_family_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "activate_tool_family";
	}

	std::string get_description() const override
	{
		std::string base_desc = "Activates a specialized tool family by name. This makes all tools belonging to that family "
					"available in the agent's context. By default, only the 'base' family is active.";

		auto families = agentlib::tool_registry::get_instance().get_all_registered_families();
		if (families.empty()) {
			return base_desc;
		}

		nlohmann::json families_arr = nlohmann::json::array();
		for (const auto &fam : families) {
			families_arr.push_back(fam);
		}

		return base_desc + "\n\n<registered_tool_families>\n" + families_arr.dump() + "\n</registered_tool_families>";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"name", {{"type", "string"}, {"description", "The name of the tool family to activate."}}}}},
			{"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			activate_tool_family_raw_args parsed = raw_json.get<activate_tool_family_raw_args>();
			if (parsed.name.empty()) {
				out_error = "Tool family name cannot be empty.";
				return false;
			}

			args_.name = parsed.name;

			// Verify if the tool family exists
			auto families = agentlib::tool_registry::get_instance().get_all_registered_families();
			if (std::find(families.begin(), families.end(), args_.name) == families.end()) {
				out_error = "Tool family '" + args_.name + "' not found. Check registered tool families.";
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
		return std::make_unique<activate_tool_family_tool>(args_);
	}

      private:
	mutable activate_tool_family_args args_;
};

REGISTER_TOOL(activate_tool_family_validator)

} // namespace tools
