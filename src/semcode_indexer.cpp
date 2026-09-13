#include "semcode_indexer.h"
#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <sqlite3.h>
#include <sstream>
#include <utility>
#include "fs_utils.h"

namespace
{

std::string extract_quoted_value(std::string_view line)
{
	size_t colon = line.find(':');
	if (colon == std::string_view::npos) {
		return "";
	}
	size_t q1 = line.find('"', colon + 1);
	if (q1 == std::string_view::npos) {
		return "";
	}
	std::string result;
	result.reserve(48);
	bool escape = false;
	for (size_t i = q1 + 1; i < line.size(); ++i) {
		char c = line[i];
		if (escape) {
			result += c;
			escape = false;
		} else if (c == '\\') {
			escape = true;
		} else if (c == '"') {
			break;
		} else {
			result += c;
		}
	}
	return result;
}

int extract_int_value(std::string_view line)
{
	size_t colon = line.find(':');
	if (colon == std::string_view::npos) {
		return 0;
	}
	size_t start = line.find_first_of("-0123456789", colon + 1);
	if (start == std::string_view::npos) {
		return 0;
	}
	size_t end = line.find_first_not_of("0123456789", start + 1);
	std::string_view num_str = (end == std::string_view::npos) ? line.substr(start) : line.substr(start, end - start);
	int val = 0;
	std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
	return val;
}

void extract_strings_from_array_line(std::string_view line, std::string &out_list)
{
	size_t pos = 0;
	size_t bracket = line.find('[');
	if (bracket != std::string_view::npos) {
		pos = bracket + 1;
	}
	while (pos < line.size()) {
		size_t q1 = line.find('"', pos);
		if (q1 == std::string_view::npos) {
			break;
		}
		std::string item;
		bool escape = false;
		size_t next_pos = q1 + 1;
		for (size_t i = q1 + 1; i < line.size(); ++i) {
			char c = line[i];
			if (escape) {
				item += c;
				escape = false;
			} else if (c == '\\') {
				escape = true;
			} else if (c == '"') {
				next_pos = i + 1;
				break;
			} else {
				item += c;
			}
		}
		if (!item.empty()) {
			if (!out_list.empty()) {
				out_list += '\n';
			}
			out_list += item;
		}
		pos = next_pos;
	}
}

void split_newline_strings(std::string_view text, std::vector<std::string> &out)
{
	out.clear();
	if (text.empty()) {
		return;
	}
	size_t start = 0;
	while (start < text.size()) {
		size_t nl = text.find('\n', start);
		if (nl == std::string_view::npos) {
			out.emplace_back(text.substr(start));
			break;
		}
		out.emplace_back(text.substr(start, nl - start));
		start = nl + 1;
	}
}

} // namespace

semcode_indexer::semcode_indexer(std::string_view db_path) : db_path_(db_path)
{
}

semcode_indexer::~semcode_indexer()
{
	close();
}

semcode_indexer::semcode_indexer(semcode_indexer &&other) noexcept : db_path_(std::move(other.db_path_)), db_(other.db_)
{
	other.db_ = nullptr;
}

semcode_indexer &semcode_indexer::operator=(semcode_indexer &&other) noexcept
{
	if (this != &other) {
		close();
		db_path_ = std::move(other.db_path_);
		db_ = other.db_;
		other.db_ = nullptr;
	}
	return *this;
}

bool semcode_indexer::open()
{
	if (db_) {
		return true;
	}
	int rc = sqlite3_open(db_path_.c_str(), &db_);
	if (rc != SQLITE_OK || !db_) {
		close();
		return false;
	}

	// Performance tuning pragmas for bulk ingest & fast in-memory query caching
	sqlite3_exec(db_, "PRAGMA synchronous = OFF;", nullptr, nullptr, nullptr);
	sqlite3_exec(db_, "PRAGMA journal_mode = MEMORY;", nullptr, nullptr, nullptr);
	sqlite3_exec(db_, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
	sqlite3_exec(db_, "PRAGMA cache_size = -64000;", nullptr, nullptr, nullptr);

	return init_schema();
}

void semcode_indexer::close()
{
	if (db_) {
		sqlite3_close(db_);
		db_ = nullptr;
	}
}

bool semcode_indexer::is_open() const noexcept
{
	return db_ != nullptr;
}

bool semcode_indexer::init_schema()
{
	if (!db_) {
		return false;
	}

	const char *schema = R"(
		CREATE TABLE IF NOT EXISTS functions (
			name        TEXT NOT NULL,
			file_path   TEXT NOT NULL,
			line_start  INTEGER NOT NULL,
			line_end    INTEGER NOT NULL,
			calls       TEXT,
			types       TEXT
		);

		CREATE TABLE IF NOT EXISTS types (
			name            TEXT NOT NULL,
			file_path       TEXT NOT NULL,
			line_start      INTEGER NOT NULL,
			line_end        INTEGER NOT NULL,
			kind            TEXT,
			underlying_type TEXT
		);

		CREATE TABLE IF NOT EXISTS meta (
			key   TEXT PRIMARY KEY,
			value TEXT
		);
	)";

	char *err_msg = nullptr;
	int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg);
	if (rc != SQLITE_OK) {
		if (err_msg) {
			sqlite3_free(err_msg);
		}
		return false;
	}
	return true;
}

size_t semcode_indexer::ingest_functions_stream(std::istream &input)
{
	if (!open()) {
		return 0;
	}

	sqlite3_stmt *stmt = nullptr;
	const char *sql = "INSERT INTO functions (name, file_path, line_start, line_end, calls, types) VALUES (?, ?, ?, ?, ?, ?);";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return 0;
	}

	sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

	std::string line;
	size_t count = 0;
	int depth = 0;
	std::string current_name;
	std::string current_file;
	int current_line_start = 0;
	int current_line_end = 0;
	std::string current_calls;
	std::string current_types;
	bool in_calls = false;
	bool in_types = false;

	while (std::getline(input, line)) {
		size_t first = line.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line).substr(first);

		if (trimmed.starts_with('{')) {
			depth++;
			if (depth == 1) {
				current_name.clear();
				current_file.clear();
				current_line_start = 0;
				current_line_end = 0;
				current_calls.clear();
				current_types.clear();
				in_calls = false;
				in_types = false;
			}
		}

		if (depth == 1) {
			if (trimmed.starts_with("\"name\":")) {
				current_name = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"file_path\":")) {
				current_file = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"line_start\":")) {
				current_line_start = extract_int_value(trimmed);
			} else if (trimmed.starts_with("\"line_end\":")) {
				current_line_end = extract_int_value(trimmed);
			} else if (trimmed.starts_with("\"calls\":")) {
				if (trimmed.find('[') != std::string_view::npos) {
					in_calls = true;
				}
			} else if (trimmed.starts_with("\"types\":")) {
				if (trimmed.find('[') != std::string_view::npos) {
					in_types = true;
				}
			}
		}

		if (in_calls) {
			extract_strings_from_array_line(trimmed, current_calls);
			if (trimmed.find(']') != std::string_view::npos) {
				in_calls = false;
			}
		}

		if (in_types) {
			extract_strings_from_array_line(trimmed, current_types);
			if (trimmed.find(']') != std::string_view::npos) {
				in_types = false;
			}
		}

		if (trimmed.starts_with('}') || trimmed.starts_with("},")) {
			if (depth == 1 && !current_name.empty()) {
				if (current_line_end < current_line_start) {
					current_line_end = current_line_start;
				}
				sqlite3_bind_text(stmt, 1, current_name.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 2, current_file.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(stmt, 3, current_line_start);
				sqlite3_bind_int(stmt, 4, current_line_end);
				sqlite3_bind_text(stmt, 5, current_calls.empty() ? nullptr : current_calls.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 6, current_types.empty() ? nullptr : current_types.c_str(), -1, SQLITE_TRANSIENT);

				sqlite3_step(stmt);
				sqlite3_reset(stmt);
				count++;
			}
			if (depth > 0) {
				depth--;
			}
		}
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

	return count;
}

size_t semcode_indexer::ingest_functions_file(const std::string &untrusted_json_path)
{
	std::string safe_path = untrusted_json_path;
	if (!fs_utils::is_regular_file(safe_path)) {
		return 0;
	}
	std::ifstream file(safe_path, std::ios::binary);
	if (!file.is_open()) {
		return 0;
	}
	return ingest_functions_stream(file);
}

size_t semcode_indexer::ingest_types_stream(std::istream &input)
{
	if (!open()) {
		return 0;
	}

	sqlite3_stmt *stmt = nullptr;
	const char *sql = "INSERT INTO types (name, file_path, line_start, line_end, kind, underlying_type) VALUES (?, ?, ?, ?, ?, ?);";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return 0;
	}

	sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

	std::string line;
	size_t count = 0;
	int depth = 0;
	std::string current_name;
	std::string current_file;
	int current_line_start = 0;
	int current_line_end = 0;
	std::string current_kind;
	std::string current_types;
	bool in_types = false;

	while (std::getline(input, line)) {
		size_t first = line.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			continue;
		}
		std::string_view trimmed = std::string_view(line).substr(first);

		if (trimmed.starts_with('{')) {
			depth++;
			if (depth == 1) {
				current_name.clear();
				current_file.clear();
				current_line_start = 0;
				current_line_end = 0;
				current_kind.clear();
				current_types.clear();
				in_types = false;
			}
		}

		if (depth == 1) {
			if (trimmed.starts_with("\"name\":")) {
				current_name = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"file_path\":")) {
				current_file = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"line_start\":")) {
				current_line_start = extract_int_value(trimmed);
			} else if (trimmed.starts_with("\"line_end\":")) {
				current_line_end = extract_int_value(trimmed);
			} else if (trimmed.starts_with("\"kind\":")) {
				current_kind = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"types\":")) {
				if (trimmed.find('[') != std::string_view::npos) {
					in_types = true;
				}
			}
		}

		if (in_types) {
			extract_strings_from_array_line(trimmed, current_types);
			if (trimmed.find(']') != std::string_view::npos) {
				in_types = false;
			}
		}

		if (trimmed.starts_with('}') || trimmed.starts_with("},")) {
			if (depth == 1 && !current_name.empty()) {
				if (current_line_end < current_line_start) {
					current_line_end = current_line_start;
				}

				// If typedef: extract first line of types as underlying_type
				std::string underlying_type;
				if (!current_types.empty()) {
					size_t nl = current_types.find('\n');
					underlying_type = (nl == std::string::npos) ? current_types : current_types.substr(0, nl);
				}

				sqlite3_bind_text(stmt, 1, current_name.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 2, current_file.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(stmt, 3, current_line_start);
				sqlite3_bind_int(stmt, 4, current_line_end);
				sqlite3_bind_text(stmt, 5, current_kind.empty() ? nullptr : current_kind.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 6, underlying_type.empty() ? nullptr : underlying_type.c_str(), -1,
						  SQLITE_TRANSIENT);

				sqlite3_step(stmt);
				sqlite3_reset(stmt);
				count++;
			}
			if (depth > 0) {
				depth--;
			}
		}
	}

	sqlite3_finalize(stmt);
	sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

	return count;
}

size_t semcode_indexer::ingest_types_file(const std::string &untrusted_json_path)
{
	std::string safe_path = untrusted_json_path;
	if (!fs_utils::is_regular_file(safe_path)) {
		return 0;
	}
	std::ifstream file(safe_path, std::ios::binary);
	if (!file.is_open()) {
		return 0;
	}
	return ingest_types_stream(file);
}

bool semcode_indexer::build_indices()
{
	if (!db_) {
		return false;
	}

	const char *indices_sql = R"(
		CREATE INDEX IF NOT EXISTS idx_func_name ON functions(name);
		CREATE INDEX IF NOT EXISTS idx_func_file ON functions(file_path);
		CREATE INDEX IF NOT EXISTS idx_type_name ON types(name);
		CREATE INDEX IF NOT EXISTS idx_type_file ON types(file_path);
	)";

	char *err_msg = nullptr;
	int rc = sqlite3_exec(db_, indices_sql, nullptr, nullptr, &err_msg);
	if (rc != SQLITE_OK) {
		if (err_msg) {
			sqlite3_free(err_msg);
		}
		return false;
	}
	return true;
}

bool semcode_indexer::set_metadata(std::string_view key, std::string_view value)
{
	if (!db_) {
		return false;
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?);";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return false;
	}
	std::string k(key);
	std::string v(value);
	sqlite3_bind_text(stmt, 1, k.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, v.c_str(), -1, SQLITE_STATIC);
	int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	return rc == SQLITE_DONE;
}

std::string semcode_indexer::get_metadata(std::string_view key) const
{
	if (!db_) {
		return "";
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT value FROM meta WHERE key = ?;";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return "";
	}
	std::string k(key);
	sqlite3_bind_text(stmt, 1, k.c_str(), -1, SQLITE_STATIC);
	std::string result;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *val = sqlite3_column_text(stmt, 0);
		if (val) {
			result = reinterpret_cast<const char *>(val);
		}
	}
	sqlite3_finalize(stmt);
	return result;
}

std::vector<semcode_function_entry> semcode_indexer::lookup_function(std::string_view name) const
{
	if (!db_ || name.empty()) {
		return {};
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT DISTINCT name, file_path, line_start, line_end, calls, types FROM functions WHERE name = ?;";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return {};
	}
	std::string n(name);
	sqlite3_bind_text(stmt, 1, n.c_str(), -1, SQLITE_STATIC);

	std::vector<semcode_function_entry> results;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		semcode_function_entry entry;
		entry.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		entry.file_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		entry.line_start = sqlite3_column_int(stmt, 2);
		entry.line_end = sqlite3_column_int(stmt, 3);
		const unsigned char *calls_txt = sqlite3_column_text(stmt, 4);
		if (calls_txt) {
			split_newline_strings(reinterpret_cast<const char *>(calls_txt), entry.calls);
		}
		const unsigned char *types_txt = sqlite3_column_text(stmt, 5);
		if (types_txt) {
			split_newline_strings(reinterpret_cast<const char *>(types_txt), entry.types);
		}
		results.push_back(std::move(entry));
	}
	sqlite3_finalize(stmt);
	return results;
}

std::vector<semcode_function_entry> semcode_indexer::lookup_functions_in_file(std::string_view file_path, int start_line,
									      int end_line) const
{
	if (!db_ || file_path.empty()) {
		return {};
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT DISTINCT name, file_path, line_start, line_end, calls, types FROM functions WHERE file_path = ? AND "
			  "line_start <= ? AND "
			  "line_end >= ?;";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return {};
	}
	std::string f(file_path);
	sqlite3_bind_text(stmt, 1, f.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 2, end_line);
	sqlite3_bind_int(stmt, 3, start_line);

	std::vector<semcode_function_entry> results;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		semcode_function_entry entry;
		entry.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		entry.file_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		entry.line_start = sqlite3_column_int(stmt, 2);
		entry.line_end = sqlite3_column_int(stmt, 3);
		const unsigned char *calls_txt = sqlite3_column_text(stmt, 4);
		if (calls_txt) {
			split_newline_strings(reinterpret_cast<const char *>(calls_txt), entry.calls);
		}
		const unsigned char *types_txt = sqlite3_column_text(stmt, 5);
		if (types_txt) {
			split_newline_strings(reinterpret_cast<const char *>(types_txt), entry.types);
		}
		results.push_back(std::move(entry));
	}
	sqlite3_finalize(stmt);
	return results;
}

std::vector<semcode_type_entry> semcode_indexer::lookup_type(std::string_view name) const
{
	if (!db_ || name.empty()) {
		return {};
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT DISTINCT name, file_path, line_start, line_end, kind, underlying_type FROM types WHERE name = ?;";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return {};
	}
	std::string n(name);
	sqlite3_bind_text(stmt, 1, n.c_str(), -1, SQLITE_STATIC);

	std::vector<semcode_type_entry> results;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		semcode_type_entry entry;
		entry.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		entry.file_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		entry.line_start = sqlite3_column_int(stmt, 2);
		entry.line_end = sqlite3_column_int(stmt, 3);
		const unsigned char *kind_txt = sqlite3_column_text(stmt, 4);
		if (kind_txt) {
			entry.kind = reinterpret_cast<const char *>(kind_txt);
		}
		const unsigned char *ut_txt = sqlite3_column_text(stmt, 5);
		if (ut_txt) {
			entry.underlying_type = reinterpret_cast<const char *>(ut_txt);
		}
		results.push_back(std::move(entry));
	}
	sqlite3_finalize(stmt);
	return results;
}
