#include <filesystem>
#include <format>
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

	std::string res = "| Database Name | Size (Bytes) |\n|---------------|--------------|\n";

	bool found = false;
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(db_dir, ec)) {
		if (entry.is_regular_file() && entry.path().extension() == ".db") {
			std::string name = entry.path().stem().string();
			auto size = entry.file_size(ec);
			res += std::format("| {} | {} |\n", name, size);
			found = true;
		}
	}

	if (!found) {
		return "No SQLite databases found for this project.";
	}

	return res;
}

} // namespace tools