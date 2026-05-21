#include "message_agent.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/tool_registry.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace tools {

struct message_agent_raw_args {
    int id;
    std::string message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(message_agent_raw_args, id, message);

class message_agent_validator : public agentlib::tool_validator {
public:
    bool is_pure() const override { return false; } // Modifies state of another agent

    std::string get_name() const override { return "message_agent"; }
    std::string get_description() const override { return "Sends a message or command to an active subagent, appending it to their processing queue."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"id", {{"type", "integer"}, {"description", "The ID of the subagent."}}},
                {"message", {{"type", "string"}, {"description", "The text message or instruction to send."}}}
            }},
            {"required", nlohmann::json::array({"id", "message"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        try {
            message_agent_raw_args raw_args = args_json.get<message_agent_raw_args>();
            if (raw_args.message.empty()) {
                out_error = "Message cannot be empty.";
                return false;
            }
            args_.id = raw_args.id;
            args_.message = raw_args.message;
            return true;
        } catch (const std::exception& e) {
            out_error = "Argument parsing error: " + std::string(e.what());
            return false;
        }
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<message_agent_tool>(args_);
    }

private:
    mutable message_agent_args args_;
};

REGISTER_TOOL(message_agent_validator)

}