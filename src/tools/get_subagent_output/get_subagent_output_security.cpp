#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "get_subagent_output.h"

namespace tools
{

struct get_subagent_output_raw_args {
	int id{-1};
	bool keep{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(get_subagent_output_raw_args, id, keep);

class get_subagent_output_validator final : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "get_subagent_output";
	}
	std::string get_description() const override
	{
		return "Retrieves the entire interaction history of a specific subagent.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"id", {{"type", "integer"}, {"description", "The ID of the subagent to query."}}},
			  {"keep",
			   {{"type", "boolean"},
				{"description", "If true, leaves the subagent active after fetching output. Defaults to false."},
				{"default", false}}}}},
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
			get_subagent_output_raw_args raw_args = args_json.get<get_subagent_output_raw_args>();
			args_.id = raw_args.id;
			args_.keep = raw_args.keep;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<get_subagent_output_tool>(args_);
	}

      private:
	mutable get_subagent_output_args args_;
};

REGISTER_TOOL(get_subagent_output_validator)

} // namespace tools