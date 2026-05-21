#include "agent_list_todos.h"
#include "../../agentlib/tool_validator.h"
#include <memory>

namespace tools {

class agent_list_todos_validator : public agentlib::tool_validator {
public:
    bool validate_arguments(const std::string& /*arguments_json*/, std::string& /*out_error*/) const override {
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool(const std::string& /*arguments_json*/) const override {
        return std::make_unique<agent_list_todos_tool>();
    }
};

static agent_list_todos_validator validator;

}

extern "C" {

const char* get_tool_name_agent_list_todos() {
    return "agent_list_todos";
}

const char* get_tool_description_agent_list_todos() {
    return "Lists all tasks currently in the AI agent's internal todo list, formatted as markdown checkboxes.";
}

const char* get_tool_parameters_schema_agent_list_todos() {
    return R"({
        "type": "object",
        "properties": {}
    })";
}

agentlib::tool_validator* get_tool_validator_agent_list_todos() {
    return &tools::validator;
}

}