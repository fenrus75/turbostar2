#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/agent_run_registry.h"
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "tools/agent_write_to_run/agent_write_to_run.h"

namespace tools
{

struct agent_write_to_run_raw_args {
	int run_id{-1};
	std::string data;
	bool output{true};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_write_to_run_raw_args, run_id, data, output);

class agent_write_to_run_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Injects input to terminal

	std::string get_name() const override
	{
		return "agent_write_to_run";
	}
	std::string get_description() const override
	{
		return "Writes/injects keyboard input sequences into the application or debugger PTY master stream (e.g. sending commands "
		       "to GDB).";
		;
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {{"Send backtrace command to a coredump session",
			 nlohmann::json{{"run_id", 2001}, {"data", "bt\n"}, {"output", true}},
			 "After agent_debug_coredump returns gdb_run_id 2001, sends 'bt\\n' to get the full stack trace, then 'frame "
			 "<N>\\ninfo locals\\n' to inspect variables."}};
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"run_id",
			   {{"type", nlohmann::json::array({"integer", "string"})},
			    {"description", "Optional execution run ID (e.g. 1010, '1010', 'run_1010') or alias: 'gdb' (latest debugger "
					    "session), 'app' (latest application run), or 'latest'. If omitted, automatically defaults to "
					    "the active debugger session (if present) or the most recent run."}}},
			  {"data", {{"type", "string"}, {"description", "The raw string data or escape sequence to inject."}}},
			  {"output",
			   {{"type", "boolean"},
			    {"description", "Optional. If true, returns the new application output that results from this command. Highly "
					    "recommended for gdb session."}}}}},
			{"required", nlohmann::json::array({"data"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		try {
			if (!args_json.contains("data") || !args_json["data"].is_string()) {
				out_error = "Missing required string field 'data'.";
				return false;
			}
			std::string raw_data = args_json["data"].get<std::string>();
			if (raw_data.length() > 4096) {
				out_error = "Payload size exceeds 4096 bytes limit.";
				return false;
			}

			int resolved_run_id = -1;
			if (ctx.run_registry) {
				if (!ctx.run_registry->resolve_from_json(args_json, agentlib::run_target_kind::debugger, resolved_run_id,
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

			bool output = false;
			if (args_json.contains("output") && args_json["output"].is_boolean()) {
				output = args_json["output"].get<bool>();
			}

			args_.run_id = resolved_run_id;
			args_.data = fs_utils::unescape_string(raw_data);
			args_.output = output;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_write_to_run_tool>(args_);
	}

      private:
	mutable agent_write_to_run_args args_;
};

REGISTER_TOOL(agent_write_to_run_validator)

} // namespace tools