#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "agent_write_to_run.h"

namespace tools
{

struct agent_write_to_run_raw_args {
	int run_id{-1};
	std::string data;
	bool output{false};
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
		return "Writes/injects keyboard input sequences into the application or debugger PTY master stream (add \n for commands to gdb!).";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"run_id", {{"type", "integer"}, {"description", "The unique execution ID returned by agent_start_app."}}},
		      {"data", {{"type", "string"}, {"description", "The raw string data or escape sequence to inject."}}},
		      {"output", {{"type", "boolean"}, {"description", "Optional. If true, returns the new application output that results from this command. Highly recommended for gdb session."}}}}},
		    {"required", nlohmann::json::array({"run_id", "data"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_write_to_run_raw_args raw = args_json.get<agent_write_to_run_raw_args>();
			if (raw.run_id < 0) {
				out_error = "Invalid run_id specified.";
				return false;
			}
			if (raw.data.length() > 4096) {
				out_error = "Payload size exceeds 4096 bytes limit.";
				return false;
			}
			args_.run_id = raw.run_id;
			args_.data = fs_utils::unescape_string(raw.data);
			args_.output = raw.output;
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