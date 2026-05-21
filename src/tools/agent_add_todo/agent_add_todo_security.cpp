#include "agent_add_todo.h"
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {
    static agentlib::single_string_tool_validator<agent_add_todo_tool> validator("text");
}

extern "C" {

const char* get_tool_name_agent_add_todo() {
    return "agent_add_todo";
}

const char* get_tool_description_agent_add_todo() {
    return "Adds a new task to the AI agent's internal todo list. Use this to track steps during complex multi-part requests.";
}

const char* get_tool_parameters_schema_agent_add_todo() {
    return R"({
        "type": "object",
        "properties": {
            "text": {
                "type": "string",
                "description": "The description of the task to add."
            }
        },
        "required": ["text"]
    })";
}

agentlib::tool_validator* get_tool_validator_agent_add_todo() {
    return &tools::validator;
}

}