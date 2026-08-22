#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_debug_coredump.h"

namespace tools
{

struct agent_debug_coredump_raw_args {
	std::string crash_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_debug_coredump_raw_args, crash_id);

class agent_debug_coredump_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Spawns external GDB process

	std::string get_name() const override
	{
		return "agent_debug_coredump";
	}
	std::string get_description() const override
	{
		return "Launches a GDB session attached to the coredump for a given crash_id. Returns JSON with gdb_run_id (e.g. 2001). Next, send 'bt\\n' via 'agent_write_to_run' to locate the crash frame N, then 'frame <N>\\ninfo locals\\n' to inspect variables, and clean up with 'agent_terminate_run'.";
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {
			{
				"Inspect backtrace and local variables after a crash",
				nlohmann::json{{"crash_id", "833323"}},
				"Launches GDB on the coredump for crash_id 833323. Next, send 'bt\\n' via agent_write_to_run to locate frame N, then 'frame N\\ninfo locals\\n' to inspect variables."
			}
		};
	}


	nlohmann::json get_parameters_schema() const override
	{
		nlohmann::json schema;
		schema["type"] = "object";
		schema["properties"]["crash_id"]["type"] = "string";
		schema["properties"]["crash_id"]["description"] = "The unique crash ID from the crash database to debug.";
		schema["required"] = nlohmann::json::array({"crash_id"});
		return schema;
	}


      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_debug_coredump_raw_args raw = args_json.get<agent_debug_coredump_raw_args>();
			if (raw.crash_id.empty()) {
				out_error = "Arguments Violation: crash_id cannot be empty.";
				return false;
			}
			for (char c : raw.crash_id) {
				if (!std::isalnum(c) && c != '-' && c != '_') {
					out_error = "Security Violation: Invalid characters in crash_id.";
					return false;
				}
			}
			args_.crash_id = raw.crash_id;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_debug_coredump_tool>(args_);
	}

      private:
	mutable agent_debug_coredump_args args_;
};

REGISTER_TOOL(agent_debug_coredump_validator)

} // namespace tools
