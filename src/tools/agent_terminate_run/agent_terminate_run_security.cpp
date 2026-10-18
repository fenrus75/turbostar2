#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/agent_run_registry.h"
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "tools/agent_terminate_run/agent_terminate_run.h"

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
		return {{"type", "object"},
			{"properties",
			 {{"run_id",
			   {{"type", nlohmann::json::array({"integer", "string"})},
			    {"description", "Optional execution run ID (e.g. 1010, '1010', 'run_1010') or alias: 'app', 'gdb', or "
					    "'latest'. If omitted, automatically defaults to the most recent active run."}}}}},
			{"required", nlohmann::json::array()}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		try {
			int resolved_run_id = -1;
			if (ctx.run_registry) {
				if (!ctx.run_registry->resolve_from_json(args_json, agentlib::run_target_kind::app, resolved_run_id,
									 out_error)) {
					return false;
				}
			} else {
				if (!args_json.contains("run_id") || !args_json["run_id"].is_number_integer()) {
					out_error = "Missing required integer field 'run_id'.";
					return false;
				}
				resolved_run_id = args_json["run_id"].get<int>();
				if (resolved_run_id < 0) {
					out_error = "Invalid run_id specified.";
					return false;
				}
			}
			args_.run_id = resolved_run_id;
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
