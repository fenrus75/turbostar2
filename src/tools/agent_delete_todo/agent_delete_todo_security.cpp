#include "agent_delete_todo.h"
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {
    static agentlib::single_string_tool_validator<agent_delete_todo_tool> validator("text");
}

extern "C" {

const char* get_tool_name_agent_delete_todo() {
    return "agent_delete_todo";
}

const char* get_tool_description_agent_delete_todo() {
    return "Deletes a task from the AI agent's internal todo list. Provide an exact string match or a unique substring.";
}

const char* get_tool_parameters_schema_agent_delete_todo() {
    return R"({
        "type": "object",
        "properties": {
            "text": {
                "type": "string",
                "description": "The exact task text or a unique substring to match."
            }
        },
        "required": ["text"]
    })";
}

agentlib::tool_validator* get_tool_validator_agent_delete_todo() {
    return &tools::validator;
}

}