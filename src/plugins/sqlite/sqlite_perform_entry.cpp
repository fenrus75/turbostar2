#include <cctype>
#include <filesystem>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <vector>
#include "fs_utils.h"
#include "plugins/sqlite/sqlite_perform.h"

namespace tools
{

namespace
{
bool validate_query_safety(const std::string &query, std::string &out_error)
{
	size_t start = query.find_first_not_of(" \t\n\r");
	if (start == std::string::npos) {
		out_error = "Query cannot be empty or whitespace-only.";
		return false;
	}

	std::string trimmed = query.substr(start);

	size_t semicolon_pos = trimmed.find(';');
	if (semicolon_pos != std::string::npos) {
		if (semicolon_pos != trimmed.size() - 1) {
			out_error = "Multi-statement queries are not allowed. Only a single SQL statement is permitted.";
			return false;
		}
		trimmed = trimmed.substr(0, trimmed.size() - 1);
	}

	std::string upper_query;
	upper_query.reserve(trimmed.size());
	for (char c : trimmed) {
		upper_query += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}

	if (upper_query.find("ATTACH") == 0) {
		out_error = "ATTACH statements are not allowed for security reasons.";
		return false;
	}

	return true;
}
} // namespace

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

	if (!validate_query_safety(query_, out_error)) {
		return false;
	}

	return true;
}

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

std::string sqlite_perform_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string db_dir = fs_utils::get_project_db_dir();
	std::filesystem::path db_path = std::filesystem::path(db_dir) / (database_ + ".db");

	std::error_code ec;
	if (!std::filesystem::exists(db_path, ec)) {
		return "Error: Database '" + database_ + "' does not exist. Call sqlite_create_db first.";
	}

	sqlite3 *db = nullptr;
	int rc = sqlite3_open(db_path.c_str(), &db);
	if (rc != SQLITE_OK) {
		std::string err = sqlite3_errmsg(db);
		sqlite3_close(db);
		return "Error opening database '" + database_ + "': " + err;
	}

	char *errmsg = nullptr;
	callback_data data;

	rc = sqlite3_exec(db, query_.c_str(), callback, &data, &errmsg);
	if (rc != SQLITE_OK) {
		std::string err = errmsg ? errmsg : "Unknown error";
		if (errmsg) {
			sqlite3_free(errmsg);
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
		ss << data.headers[i] << (i + 1 == data.headers.size() ? " |\n" : " | ");
	}

	ss << "|";
	for (size_t i = 0; i < data.headers.size(); i++) {
		ss << "---|";
	}
	ss << "\n";

	for (const auto &row : data.rows) {
		ss << "| ";
		for (size_t i = 0; i < row.size(); i++) {
			ss << row[i] << (i + 1 == row.size() ? " |\n" : " | ");
		}
	}

	return ss.str();
}

} // namespace tools
