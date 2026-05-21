#include "sqlite_delete_db.h"
#include "../../fs_utils.h"
#include "../../agentlib/single_string_tool_validator.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

class sqlite_delete_db_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "sqlite_delete_db"; }
    std::string get_description() const override { return "Deletes an existing SQLite database for the project."; }
    std::string get_parameter_name() const override { return "database"; }
    std::string get_parameter_description() const override { return "The simple name of the database to delete (no paths or extensions)."; }

    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        if (!fs_utils::is_valid_db_name(arg)) {
            out_error = "Database name must contain only a-z, A-Z, 0-9, _, and -.";
            return false;
        }
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override {
        return std::make_unique<sqlite_delete_db_tool>(arg);
    }
};

REGISTER_TOOL(sqlite_delete_db_validator)

}