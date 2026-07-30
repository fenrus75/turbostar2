#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "kill_subagent.h"

namespace tools
{

struct kill_subagent_raw_args {
	int id{-1};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(kill_subagent_raw_args, id);

class kill_subagent_validator final : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "kill_subagent";
	}
	std::string get_description() const override
	{
		return "Closes and terminates a specific subagent by its ID.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"id", {{"type", "integer"}, {"description", "The ID of the subagent to terminate."}}}}},
			{"required", nlohmann::json::array({"id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		if (!args_json.contains("id")) {
			out_error = "Argument parsing error: missing required field 'id'";
			return false;
		}
		try {
			kill_subagent_raw_args raw_args = args_json.get<kill_subagent_raw_args>();
			args_.id = raw_args.id;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<kill_subagent_tool>(args_);
	}

      private:
	mutable kill_subagent_args args_;
};

REGISTER_TOOL(kill_subagent_validator)

} // namespace tools