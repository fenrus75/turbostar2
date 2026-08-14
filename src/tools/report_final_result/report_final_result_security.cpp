#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "report_final_result.h"

namespace tools
{

struct report_final_result_raw_args {
	std::string result;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(report_final_result_raw_args, result);

class report_final_result_validator final : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "report_final_result";
	}
	std::string get_description() const override
	{
		return "Reports the final result or summary of the completed task back to the parent agent.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"result", {{"type", "string"}, {"description", "The final result or outcome to report."}}}}},
			{"required", nlohmann::json::array({"result"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		if (!args_json.contains("result")) {
			out_error = "Argument parsing error: missing required field 'result'";
			return false;
		}
		try {
			report_final_result_raw_args raw_args = args_json.get<report_final_result_raw_args>();
			args_.result = raw_args.result;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<report_final_result_tool>(args_);
	}

      private:
	mutable report_final_result_args args_;
};

REGISTER_TOOL(report_final_result_validator)

} // namespace tools
