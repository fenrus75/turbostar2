#include <filesystem>
#include <sstream>
#include "fs_utils.h"
#include "plugins/sqlite/sqlite_list_db.h"

namespace tools
{

bool sqlite_list_db_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string sqlite_list_db_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string db_dir = fs_utils::get_project_db_dir();

	std::error_code ec;
	if (!std::filesystem::exists(db_dir, ec) || !std::filesystem::is_directory(db_dir, ec)) {
		return "No SQLite databases found for this project.";
	}

	std::stringstream ss;
	ss << "| Database Name | Size (Bytes) |\n";
	ss << "|---------------|--------------|\n";

	bool found = false;
	for (const auto &entry : std::filesystem::directory_iterator(db_dir, ec)) {
		if (ec) {
			break;
		}
		if (entry.is_regular_file(ec) && entry.path().extension() == ".db") {
			std::string stem = entry.path().stem().string();
			uint64_t size = entry.file_size(ec);
			ss << "| " << stem << " | " << size << " |\n";
			found = true;
		}
	}

	if (!found) {
		return "No SQLite databases found for this project.";
	}

	return ss.str();
}

} // namespace tools
