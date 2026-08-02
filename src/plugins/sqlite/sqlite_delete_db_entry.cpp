#include <filesystem>
#include "fs_utils.h"
#include "plugins/sqlite/sqlite_delete_db.h"

namespace tools
{

sqlite_delete_db_tool::sqlite_delete_db_tool(std::string database) : database_(std::move(database))
{
}

bool sqlite_delete_db_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (!fs_utils::is_valid_db_name(database_)) {
		out_error = "Database name must contain only a-z, A-Z, 0-9, _, and -.";
		return false;
	}
	return true;
}

std::string sqlite_delete_db_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string db_dir = fs_utils::get_project_db_dir();
	std::filesystem::path db_path = std::filesystem::path(db_dir) / (database_ + ".db");

	std::error_code ec;
	if (!std::filesystem::exists(db_path, ec)) {
		return "Error: Database '" + database_ + "' does not exist.";
	}

	if (!std::filesystem::remove(db_path, ec)) {
		return "Error deleting database '" + database_ + "': " + ec.message();
	}

	return "Database '" + database_ + "' deleted successfully.";
}

} // namespace tools
