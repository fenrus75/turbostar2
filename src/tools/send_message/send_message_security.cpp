#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
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
	bool is_pure() const override
	{
		return true;
	} // Subagent messaging does not modify project codebase

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
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			send_message_raw_args raw_args = args_json.get<send_message_raw_args>();
			if (raw_args.message.empty()) {
				out_error = "Message cannot be empty.";
				return false;
			}

			// SECURITY CHECK: Enforce maximum message length to prevent DoS attacks
			constexpr size_t MAX_MESSAGE_LENGTH = 100 * 1024; // 100KB limit
			if (raw_args.message.length() > MAX_MESSAGE_LENGTH) {
				out_error = "Message exceeds maximum allowed length of " + std::to_string(MAX_MESSAGE_LENGTH) +
				            " bytes (" + std::to_string(raw_args.message.length()) + " bytes provided).";
				return false;
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
