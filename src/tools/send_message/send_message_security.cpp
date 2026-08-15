#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "agentlib/ai_agent.h"
#include "fs_utils.h"
#include "send_message.h"

namespace tools
{

struct send_message_raw_args {
	int id;
	std::string message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(send_message_raw_args, id, message);

class send_message_validator : public agentlib::tool_validator
{
      public:
	// Pure Domain 2 (Agent & Workflow State): Enqueues subagent interaction message in session memory.
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "send_message";
	}
	std::string get_description() const override
	{
		return "Sends a message or command to an active subagent, appending it to their processing queue.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"id", {{"type", "integer"}, {"description", "The ID of the subagent."}}},
			  {"message", {{"type", "string"}, {"description", "The text message or instruction to send."}}}}},
			{"required", nlohmann::json::array({"id", "message"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		if (ctx.active_agent && ctx.active_agent->is_read_only()) {
			out_error = "Security Violation: Agent in read-only mode cannot send messages to subagents.";
			return false;
		}

		try {
			send_message_raw_args raw_args = args_json.get<send_message_raw_args>();
			if (raw_args.message.empty()) {
				out_error = "Message cannot be empty.";
				return false;
			}
			if (raw_args.message.length() > 100000) {
				out_error = "Validation Error: Message exceeds maximum length of 100,000 characters.";
				return false;
			}
			for (unsigned char c : raw_args.message) {
				if (c < 32 && c != 9 && c != 10 && c != 13) {
					out_error = "Security Violation: Message contains unsafe control characters.";
					return false;
				}
				if (c == 127) {
					out_error = "Security Violation: Message contains unsafe control characters.";
					return false;
				}
			}
			args_.id = raw_args.id;
			args_.message = raw_args.message;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<send_message_tool>(args_);
	}

      private:
	mutable send_message_args args_;
};

REGISTER_TOOL(send_message_validator)

} // namespace tools
