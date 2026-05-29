#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_terminate_run.h"

namespace tools
{

struct agent_terminate_run_raw_args {
	int run_id{-1};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(agent_terminate_run_raw_args, run_id);

class agent_terminate_run_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Stops/erases a process run

	std::string get_name() const override
	{
		return "agent_terminate_run";
	}
	std::string get_description() const override
	{
		return "Terminates/stops a running process and closes its window based on its run ID.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"run_id", {{"type", "integer"}, {"description", "The unique execution ID returned by agent_start_app."}}}}},
		    {"required", nlohmann::json::array({"run_id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_terminate_run_raw_args raw = args_json.get<agent_terminate_run_raw_args>();
			if (raw.run_id < 0) {
				out_error = "Invalid run_id specified.";
				return false;
			}
			args_.run_id = raw.run_id;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_terminate_run_tool>(args_);
	}

      private:
	mutable agent_terminate_run_args args_;
};

REGISTER_TOOL(agent_terminate_run_validator)

} // namespace tools
