#include "../../event_queue.h"
#include "../../fs_utils.h"
#include "agent_set_status.h"

namespace tools
{

agent_set_status_tool::agent_set_status_tool(agent_set_status_args args)
    : llm_tool_action("Setting status to: " + args.message), args_(std::move(args))
{
}

std::string agent_set_status_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.queue) {
		editor_event ev;
		ev.type = event_type::set_transient_status;
		ev.payload = args_.message;
		ctx.queue->push(ev);
		set_success(ctx);
		return "Status message updated.";
	}
	set_failure(ctx, "Event queue not available.");
	return "Error: Event queue not available.";
}

class agent_set_status_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_set_status";
	}
	std::string get_description() const override
	{
		return "Sets a brief status message in the editor's status bar to inform the user of progress.";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"message", {{"type", "string"}, {"description", "The brief status message (e.g., 'Analyzing code...')."}}}}},
			{"required", {"message"}}};
	}

	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context & /*ctx*/, std::string &out_error) const override
	{
		if (!args.is_object() || !args.contains("message") || !args["message"].is_string()) {
			out_error = "Missing or invalid 'message' argument.";
			return false;
		}

		std::string msg = args["message"].get<std::string>();
		const size_t MAX_STATUS_LENGTH = 256;
		if (msg.length() > MAX_STATUS_LENGTH) {
			out_error = "Security Violation: Status message too long.";
			return false;
		}

		// Security check: Reject empty or whitespace-only messages
		if (msg.empty() || msg.find_first_not_of(" \t\n\r") == std::string::npos) {
			out_error = "Security Violation: Status message cannot be empty or whitespace-only.";
			return false;
		}

		if (!fs_utils::is_safe_for_ui(msg)) {
			out_error = "Security Violation: Status message contains unsafe control characters or escape sequences.";
			return false;
		}

		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override
	{
		agent_set_status_args parsed_args;
		parsed_args.message = args["message"].get<std::string>();
		return std::make_unique<agent_set_status_tool>(std::move(parsed_args));
	}
};

REGISTER_TOOL(agent_set_status_validator);

} // namespace tools
