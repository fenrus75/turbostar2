#include "sqlite_create_db.h"
#include "../../fs_utils.h"
#include <filesystem>
#include <sqlite3.h>

namespace tools {

sqlite_create_db_tool::sqlite_create_db_tool(std::string database) : database_(std::move(database)) {}

bool sqlite_create_db_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (database_.empty() || database_.find('/') != std::string::npos || database_.find('\\') != std::string::npos) {
        out_error = "Invalid database name. Use a simple alphanumeric filename without paths.";
        return false;
    }
    return true;
}

std::string sqlite_create_db_tool::execute(agentlib::tool_context& /*ctx*/) {
    std::string db_dir = fs_utils::get_project_db_dir();
    std::string db_path = (std::filesystem::path(db_dir) / (database_ + ".db")).string();

    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db);
        sqlite3_close(db);
        return "Error creating database: " + err;
    }
    
    sqlite3_close(db);
    return "Database '" + database_ + "' created successfully.";
}

} // namespace tools