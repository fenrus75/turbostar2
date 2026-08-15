#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "agent_start_app.h"

namespace tools
{

struct agent_start_app_raw_args {
	std::string args;
	bool debugger{false};
	int wait_for_time{0};
	bool collect_performance{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_start_app_raw_args, args, debugger, wait_for_time, collect_performance);

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
		return "Starts the main application executable, optionally under GDB debugging with split screen or performance sampling. Returns JSON with app_run_id and gdb_run_id. In GDB mode, the app starts paused, send 'continue' to gdb to start the application.";
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
			{"default", false}}},
		      {"wait_for_time",
		       {{"type", "integer"},
			{"description",
			 "Optional time in seconds to wait for the application to finish after starting. Defaults to 0 (async execution, max 300)."},
			{"default", 0}}},
		      {"collect_performance",
		       {{"type", "boolean"},
			{"description",
			 "If true, enables performance CPU cycle profiling sampling via LD_PRELOAD during execution."},
			{"default", false}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_start_app_raw_args raw = args_json.get<agent_start_app_raw_args>();
			if (raw.args.length() > 1024) {
				out_error = "Validation Error: args parameter exceeds maximum length of 1024 characters.";
				return false;
			}
			// Check for command injection/chaining characters in args
			for (char c : raw.args) {
				if (c == ';' || c == '&' || c == '|' || c == '`' || c == '$' ||
				    c == '<' || c == '>' || c == '(' || c == ')' || c == '\'' || c == '"' ||
				    c == '\\' || c == '\n' || c == '\r') {
					out_error = "Security Violation: Unsafe characters detected in arguments.";
					return false;
				}
			}
			if (raw.wait_for_time < 0 || raw.wait_for_time > 300) {
				out_error = "Validation Error: wait_for_time must be between 0 and 300 seconds.";
				return false;
			}
			args_.args = raw.args;
			args_.debugger = raw.debugger;
			args_.wait_for_time = raw.wait_for_time;
			args_.collect_performance = raw.collect_performance;
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