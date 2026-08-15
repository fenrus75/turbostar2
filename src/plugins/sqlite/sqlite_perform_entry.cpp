#include <cctype>
#include <filesystem>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <vector>
#include "fs_utils.h"
#include "plugins/sqlite/sqlite_perform.h"
#include "plugins/sqlite/sqlite_query_validation.h"

namespace tools
{

namespace
{
std::string sanitize_cell(std::string_view val)
{
	std::string res;
	for (char c : val) {
		if (c == '|') {
			res += "&#124;";
		} else if (c == '\n' || c == '\r' || c == '\t') {
			res += " ";
		} else if (static_cast<unsigned char>(c) < 32) {
			// skip control chars
		} else {
			res += c;
		}
	}
	return res;
}
} // namespace

struct callback_data {
	std::vector<std::string> headers;
	std::vector<std::vector<std::string>> rows;
	bool headers_captured{false};
};

static int callback(void *data_ptr, int argc, char **argv, char **azColName)
{
	auto *data = static_cast<callback_data *>(data_ptr);

	if (!data->headers_captured) {
		for (int i = 0; i < argc; i++) {
			data->headers.push_back(azColName[i] ? azColName[i] : "NULL");
		}
		data->headers_captured = true;
	}

	std::vector<std::string> row;
	for (int i = 0; i < argc; i++) {
		row.push_back(argv[i] ? argv[i] : "NULL");
	}
	data->rows.push_back(row);

	return 0;
}

sqlite_perform_tool::sqlite_perform_tool(std::string database, std::string query)
    : database_(std::move(database)), query_(std::move(query))
{
}

bool sqlite_perform_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (!fs_utils::is_valid_db_name(database_)) {
		out_error = "Database name must contain only a-z, A-Z, 0-9, _, and -.";
		return false;
	}

	return validate_query_safety(query_, out_error);
}

std::string sqlite_perform_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string db_dir = fs_utils::get_project_db_dir();
	std::filesystem::create_directories(db_dir);
	std::string db_path = db_dir + "/" + database_ + ".db";

	sqlite3 *db = nullptr;
	if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
		std::string err = db ? sqlite3_errmsg(db) : "Unknown error";
		if (db)
			sqlite3_close(db);
		return "Error opening database: " + err;
	}

	callback_data data;
	char *err_msg = nullptr;

	if (sqlite3_exec(db, query_.c_str(), callback, &data, &err_msg) != SQLITE_OK) {
		std::string err = err_msg ? err_msg : "Unknown error";
		if (err_msg) {
			sqlite3_free(err_msg);
		}
		sqlite3_close(db);
		return "Error executing query: " + err;
	}

	sqlite3_close(db);

	if (data.rows.empty()) {
		return "Query executed successfully. (0 rows returned)";
	}

	std::stringstream ss;
	ss << "| ";
	for (size_t i = 0; i < data.headers.size(); i++) {
		ss << sanitize_cell(data.headers[i]) << (i + 1 == data.headers.size() ? " |\n" : " | ");
	}

	ss << "|";
	for (size_t i = 0; i < data.headers.size(); i++) {
		ss << "---|";
	}
	ss << "\n";

	for (const auto &row : data.rows) {
		ss << "| ";
		for (size_t i = 0; i < row.size(); i++) {
			ss << sanitize_cell(row[i]) << (i + 1 == row.size() ? " |\n" : " | ");
		}
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("sqlite_result", ss.str());
}

} // namespace tools
