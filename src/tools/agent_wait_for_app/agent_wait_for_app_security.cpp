#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "tools/agent_wait_for_app/agent_wait_for_app.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace tools
{

struct agent_wait_for_app_raw_args {
	int run_id{-1};
	std::string type{"ended"};
	int timeout_sec{30};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_wait_for_app_raw_args, run_id, type, timeout_sec);

class agent_wait_for_app_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_wait_for_app";
	}

	std::string get_description() const override
	{
		return "Waits until a running process has either ended/crashed or reached a settled state without output for at least 500ms.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"run_id", {{"type", "integer"}, {"description", "The unique execution ID returned by agent_start_app."}}},
		      {"type", {{"type", "string"}, {"enum", {"ended", "settled"}}, {"default", "ended"}, {"description", "The wait condition: 'ended' waits for process termination or crash, 'settled' waits for either termination or 500ms of no output."}}},
		      {"timeout_sec", {{"type", "integer"}, {"default", 30}, {"description", "Maximum time in seconds to wait before returning status 'timeout'."}}}}},
		    {"required", nlohmann::json::array({"run_id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_wait_for_app_raw_args raw = args_json.get<agent_wait_for_app_raw_args>();
			if (raw.run_id < 0) {
				out_error = "Invalid run_id specified.";
				return false;
			}
			if (raw.type != "ended" && raw.type != "settled") {
				out_error = "Invalid type parameter. Expected 'ended' or 'settled'.";
				return false;
			}
			if (raw.timeout_sec <= 0 || raw.timeout_sec > 300) {
				out_error = "timeout_sec must be between 1 and 300.";
				return false;
			}
			args_.run_id = raw.run_id;
			args_.type = raw.type;
			args_.timeout_sec = raw.timeout_sec;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_wait_for_app_tool>(args_);
	}

      private:
	mutable agent_wait_for_app_args args_;
};

REGISTER_TOOL(agent_wait_for_app_validator)

} // namespace tools
