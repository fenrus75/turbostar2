#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_start_app.h"

namespace tools
{

struct agent_start_app_raw_args {
	std::string args;
	bool debugger{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_start_app_raw_args, args, debugger);

class agent_start_app_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Spawns external processes

	std::string get_name() const override
	{
		return "agent_start_app";
	}
	std::string get_description() const override
	{
		return "Starts the main application executable, optionally under GDB debugging with split screen. Returns JSON with app_run_id and gdb_run_id.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"args", {{"type", "string"}, {"description", "Command line arguments to pass to the application. Optional."}}},
		      {"debugger",
		       {{"type", "boolean"},
			{"description",
			 "If true, starts the application with a split screen debugger (GDB/GDBServer). Defaults to false."},
			{"default", false}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_start_app_raw_args raw = args_json.get<agent_start_app_raw_args>();
			args_.args = raw.args;
			args_.debugger = raw.debugger;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_start_app_tool>(args_);
	}

      private:
	mutable agent_start_app_args args_;
};

REGISTER_TOOL(agent_start_app_validator)

} // namespace tools
