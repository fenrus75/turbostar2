#include <filesystem>
#include <sqlite3.h>
#include <sstream>
#include "../../fs_utils.h"
#include "sqlite_perform.h"

namespace tools
{

sqlite_perform_tool::sqlite_perform_tool(std::string database, std::string query) : database_(std::move(database)), query_(std::move(query))
{
}

bool sqlite_perform_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (!fs_utils::is_valid_db_name(database_)) {
		out_error = "Invalid database name. Use only a-z, A-Z, 0-9, _, and -.";
		return false;
	}
	if (query_.empty()) {
		out_error = "Query cannot be empty.";
		return false;
	}
	return true;
}

std::string sqlite_perform_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string db_dir = fs_utils::get_project_db_dir();
	std::string db_path = (std::filesystem::path(db_dir) / (database_ + ".db")).string();

	if (!std::filesystem::exists(db_path)) {
		return "Error: Database '" + database_ + "' does not exist.";
	}

	sqlite3 *db = nullptr;
	int rc = sqlite3_open(db_path.c_str(), &db);
	if (rc != SQLITE_OK) {
		std::string err = sqlite3_errmsg(db);
		sqlite3_close(db);
		return "Error opening database: " + err;
	}

	struct callback_data {
		std::ostringstream oss;
		bool header_written = false;
		int row_count = 0;
	};

	callback_data data;

	auto callback = [](void *data_ptr, int argc, char **argv, char **azColName) -> int {
		auto *d = static_cast<callback_data *>(data_ptr);

		if (!d->header_written) {
			d->oss << "|";
			for (int i = 0; i < argc; i++) {
				d->oss << " " << (azColName[i] ? azColName[i] : "") << " |";
			}
			d->oss << "\n|";
			for (int i = 0; i < argc; i++) {
				d->oss << "---|";
			}
			d->oss << "\n";
			d->header_written = true;
		}

		d->oss << "|";
		for (int i = 0; i < argc; i++) {
			std::string val = argv[i] ? argv[i] : "NULL";
			// Very basic escape for markdown tables (replace newlines and pipes)
			for (char &c : val) {
				if (c == '\n' || c == '\r')
					c = ' ';
				else if (c == '|')
					c = '/';
			}
			d->oss << " " << val << " |";
		}
		d->oss << "\n";
		d->row_count++;

		return 0;
	};

	char *errmsg = nullptr;
	rc = sqlite3_exec(db, query_.c_str(), callback, &data, &errmsg);

	if (rc != SQLITE_OK) {
		std::string err = errmsg ? errmsg : "Unknown execution error";
		if (errmsg)
			sqlite3_free(errmsg);
		sqlite3_close(db);
		return "Error executing query: " + err;
	}

	sqlite3_close(db);

	if (data.row_count > 0) {
		return data.oss.str();
	}

	return "Query executed successfully. (0 rows returned)";
}

} // namespace tools