#include "sqlite_delete_db.h"
#include "../../fs_utils.h"
#include <filesystem>

namespace tools {

sqlite_delete_db_tool::sqlite_delete_db_tool(std::string database) : database_(std::move(database)) {}

bool sqlite_delete_db_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (!fs_utils::is_valid_db_name(database_)) {
        out_error = "Invalid database name. Use only a-z, A-Z, 0-9, _, and -.";
        return false;
    }
    return true;
}

std::string sqlite_delete_db_tool::execute(agentlib::tool_context& /*ctx*/) {
    std::string db_dir = fs_utils::get_project_db_dir();
    std::string db_path = (std::filesystem::path(db_dir) / (database_ + ".db")).string();

    std::error_code ec;
    if (std::filesystem::remove(db_path, ec)) {
        return "Database '" + database_ + "' deleted successfully.";
    } else {
        return "Failed to delete database: " + ec.message();
    }
}

} // namespace tools