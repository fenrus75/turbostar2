#include <filesystem>
#include <sstream>
#include "../../fs_utils.h"
#include "sqlite_list_db.h"

namespace tools
{

bool sqlite_list_db_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string sqlite_list_db_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string db_dir = fs_utils::get_project_db_dir();

	std::ostringstream oss;
	oss << "| Database Name | Size (Bytes) |\n";
	oss << "|---------------|--------------|\n";

	bool found = false;
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(db_dir, ec)) {
		if (entry.is_regular_file() && entry.path().extension() == ".db") {
			std::string name = entry.path().stem().string();
			auto size = entry.file_size(ec);
			oss << "| " << name << " | " << size << " |\n";
			found = true;
		}
	}

	if (!found) {
		return "No SQLite databases found for this project.";
	}

	return oss.str();
}

} // namespace tools