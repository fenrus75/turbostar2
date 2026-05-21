#include "sqlite_create_db.h"
#include "../../agentlib/single_string_tool_validator.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

class sqlite_create_db_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "sqlite_create_db"; }
    std::string get_description() const override { return "Creates a new persistent SQLite database for the project."; }
    std::string get_parameter_name() const override { return "database"; }
    std::string get_parameter_description() const override { return "The simple name of the database to create (no paths or extensions)."; }

    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        if (arg.empty() || arg.find('/') != std::string::npos || arg.find('\\') != std::string::npos) {
            out_error = "Database name must be a simple filename without paths or slashes.";
            return false;
        }
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override {
        return std::make_unique<sqlite_create_db_tool>(arg);
    }
};

REGISTER_TOOL(sqlite_create_db_validator)

}