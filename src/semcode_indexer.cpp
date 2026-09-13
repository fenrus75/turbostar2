#include "semcode_indexer.h"
#include <algorithm>
#include <charconv>
#include <filesystem>
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

	std::error_code ec;
	if (fs_utils::is_regular_file(db_path_)) {
		std::filesystem::last_write_time(db_path_, std::filesystem::file_time_type::clock::now(), ec);
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
	std::string current_git_file_hash;
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
				current_git_file_hash.clear();
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
			} else if (trimmed.starts_with("\"git_file_hash\":")) {
				current_git_file_hash = extract_quoted_value(trimmed);
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
				if (blob_filter_enabled_ && !current_git_file_hash.empty() &&
				    !valid_blob_hashes_.contains(current_git_file_hash)) {
					// Stale function from an older historical commit
					if (depth > 0) {
						depth--;
					}
					continue;
				}

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
	std::string current_git_file_hash;
	int current_line_start = 0;
	int current_line_end = 0;
	std::string current_kind;
	std::string current_underlying;
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
				current_git_file_hash.clear();
				current_line_start = 0;
				current_line_end = 0;
				current_kind.clear();
				current_underlying.clear();
				current_types.clear();
				in_types = false;
			}
		}

		if (depth == 1) {
			if (trimmed.starts_with("\"name\":")) {
				current_name = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"file_path\":")) {
				current_file = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"git_file_hash\":")) {
				current_git_file_hash = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"line_start\":")) {
				current_line_start = extract_int_value(trimmed);
			} else if (trimmed.starts_with("\"line_end\":")) {
				current_line_end = extract_int_value(trimmed);
			} else if (trimmed.starts_with("\"kind\":")) {
				current_kind = extract_quoted_value(trimmed);
			} else if (trimmed.starts_with("\"underlying_type\":")) {
				current_underlying = extract_quoted_value(trimmed);
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
				if (blob_filter_enabled_ && !current_git_file_hash.empty() &&
				    !valid_blob_hashes_.contains(current_git_file_hash)) {
					// Stale type from an older historical commit
					if (depth > 0) {
						depth--;
					}
					continue;
				}

				if (current_line_end < current_line_start) {
					current_line_end = current_line_start;
				}

				// If typedef: extract first line of types as underlying_type if not explicitly set
				std::string underlying_type = current_underlying;
				if (underlying_type.empty() && !current_types.empty()) {
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
	std::lock_guard<std::mutex> lock(db_mutex_);
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
	std::lock_guard<std::mutex> lock(db_mutex_);
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
	std::lock_guard<std::mutex> lock(db_mutex_);
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

std::vector<semcode_function_entry> semcode_indexer::lookup_callers(std::string_view callee_name) const
{
	std::lock_guard<std::mutex> lock(db_mutex_);
	if (!db_ || callee_name.empty()) {
		return {};
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT DISTINCT name, file_path, line_start, line_end, calls, types FROM functions "
			  "WHERE calls = ? "
			  "   OR calls LIKE (? || char(10) || '%') "
			  "   OR calls LIKE ('%' || char(10) || ? || char(10) || '%') "
			  "   OR calls LIKE ('%' || char(10) || ?);";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return {};
	}
	std::string c(callee_name);
	sqlite3_bind_text(stmt, 1, c.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, c.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, c.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, c.c_str(), -1, SQLITE_STATIC);

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

std::vector<semcode_function_entry> semcode_indexer::lookup_functions_by_prefix(std::string_view prefix, size_t limit) const
{
	std::lock_guard<std::mutex> lock(db_mutex_);
	if (!db_ || prefix.empty()) {
		return {};
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT DISTINCT name, file_path, line_start, line_end FROM functions "
			  "WHERE name LIKE (? || '%') LIMIT ?;";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return {};
	}
	std::string p(prefix);
	sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));

	std::vector<semcode_function_entry> results;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		semcode_function_entry entry;
		entry.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
		entry.file_path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
		entry.line_start = sqlite3_column_int(stmt, 2);
		entry.line_end = sqlite3_column_int(stmt, 3);
		results.push_back(std::move(entry));
	}
	sqlite3_finalize(stmt);
	return results;
}

std::vector<semcode_type_entry> semcode_indexer::lookup_types_by_prefix(std::string_view prefix, size_t limit) const
{
	std::lock_guard<std::mutex> lock(db_mutex_);
	if (!db_ || prefix.empty()) {
		return {};
	}
	sqlite3_stmt *stmt = nullptr;
	const char *sql = "SELECT DISTINCT name, file_path, line_start, line_end, kind, underlying_type FROM types "
			  "WHERE name LIKE (? || '%') LIMIT ?;";
	if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return {};
	}
	std::string p(prefix);
	sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));

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

size_t semcode_indexer::prune_cache_directory(const std::string &untrusted_dir, size_t max_dbs)
{
	std::string safe_dir = untrusted_dir;
	std::error_code ec;
	if (safe_dir.empty() || !std::filesystem::is_directory(safe_dir, ec)) {
		return 0;
	}

	struct db_file_entry {
		std::filesystem::path path;
		std::filesystem::file_time_type mtime;
	};

	std::vector<db_file_entry> dbs;
	for (const auto &entry : std::filesystem::directory_iterator(safe_dir, ec)) {
		if (ec) {
			break;
		}
		if (!entry.is_regular_file(ec)) {
			continue;
		}
		std::string filename = entry.path().filename().string();
		if (filename.ends_with(".db")) {
			auto mtime = entry.last_write_time(ec);
			if (!ec) {
				dbs.push_back({entry.path(), mtime});
			}
		}
	}

	if (dbs.size() <= max_dbs) {
		return 0;
	}

	// Sort descending by mtime (most recently modified/accessed first)
	std::sort(dbs.begin(), dbs.end(), [](const db_file_entry &a, const db_file_entry &b) {
		if (a.mtime != b.mtime) {
			return a.mtime > b.mtime;
		}
		return a.path > b.path;
	});

	size_t removed_count = 0;
	for (size_t i = max_dbs; i < dbs.size(); ++i) {
		const auto &db_path = dbs[i].path;
		// Remove main .db file
		if (std::filesystem::remove(db_path, ec)) {
			removed_count++;
		}
		// Remove potential SQLite companion files (-journal, -wal, -shm)
		std::filesystem::remove(db_path.string() + "-journal", ec);
		std::filesystem::remove(db_path.string() + "-wal", ec);
		std::filesystem::remove(db_path.string() + "-shm", ec);
	}

	return removed_count;
}

void semcode_indexer::set_valid_blob_hashes(std::unordered_set<std::string> hashes)
{
	valid_blob_hashes_ = std::move(hashes);
	blob_filter_enabled_ = !valid_blob_hashes_.empty();
}

void semcode_indexer::clear_valid_blob_hashes() noexcept
{
	valid_blob_hashes_.clear();
	blob_filter_enabled_ = false;
}

bool semcode_indexer::has_blob_filter() const noexcept
{
	return blob_filter_enabled_ && !valid_blob_hashes_.empty();
}

bool semcode_indexer::load_valid_blobs_from_git(const std::string &untrusted_project_dir, std::string_view git_ref)
{
	std::string safe_project = untrusted_project_dir;
	std::error_code ec;
	if (safe_project.empty() || !std::filesystem::is_directory(safe_project, ec)) {
		return false;
	}

	valid_blob_hashes_.clear();

	// 1. Load blobs from git tree at git_ref
	std::string ls_cmd = fs_utils::format_command("git -C {} ls-tree -r {} 2>/dev/null", safe_project, git_ref);
	std::string ls_out = fs_utils::execute_command_sync(ls_cmd, 30);

	std::string_view out_view(ls_out);
	size_t pos = 0;
	while (pos < out_view.size()) {
		size_t next_nl = out_view.find('\n', pos);
		std::string_view line = (next_nl == std::string_view::npos) ? out_view.substr(pos) : out_view.substr(pos, next_nl - pos);
		size_t blob_pos = line.find(" blob ");
		if (blob_pos != std::string_view::npos) {
			size_t hash_start = blob_pos + 6;
			if (hash_start + 40 <= line.size()) {
				valid_blob_hashes_.emplace(line.substr(hash_start, 40));
			}
		}
		if (next_nl == std::string_view::npos) {
			break;
		}
		pos = next_nl + 1;
	}

	// 2. Also hash any uncommitted / modified files in the working directory
	std::string status_cmd = fs_utils::format_command("git -C {} status --porcelain 2>/dev/null", safe_project);
	std::string status_out = fs_utils::execute_command_sync(status_cmd, 10);
	if (!status_out.empty()) {
		std::string_view st_view(status_out);
		size_t st_pos = 0;
		while (st_pos < st_view.size()) {
			size_t next_nl = st_view.find('\n', st_pos);
			std::string_view line =
			    (next_nl == std::string_view::npos) ? st_view.substr(st_pos) : st_view.substr(st_pos, next_nl - st_pos);
			if (line.size() > 3) {
				std::string_view file_rel = line.substr(3);
				size_t arrow = file_rel.find(" -> ");
				if (arrow != std::string_view::npos) {
					file_rel = file_rel.substr(arrow + 4);
				}
				std::string hash_cmd =
				    fs_utils::format_command("git -C {} hash-object {} 2>/dev/null", safe_project, file_rel);
				std::string h = fs_utils::execute_command_sync(hash_cmd, 5);
				size_t h_nl = h.find_first_of("\r\n");
				if (h_nl != std::string::npos) {
					h = h.substr(0, h_nl);
				}
				if (h.size() == 40) {
					valid_blob_hashes_.insert(h);
				}
			}
			if (next_nl == std::string_view::npos) {
				break;
			}
			st_pos = next_nl + 1;
		}
	}

	blob_filter_enabled_ = !valid_blob_hashes_.empty();
	return blob_filter_enabled_;
}

void semcode_indexer::touch_database(const std::string &untrusted_db_path)
{
	std::string safe_path = untrusted_db_path;
	std::error_code ec;
	if (fs_utils::is_regular_file(safe_path)) {
		std::filesystem::last_write_time(safe_path, std::filesystem::file_time_type::clock::now(), ec);
	}
}

bool semcode_indexer::build_from_project(const std::string &untrusted_project_dir, std::string_view semcode_bin, size_t max_dbs)
{
	std::string safe_project = untrusted_project_dir;
	std::error_code ec;
	if (safe_project.empty() || !std::filesystem::is_directory(safe_project, ec)) {
		return false;
	}

	// 1. Resolve git commit HEAD if in a git repository
	std::string git_cmd = fs_utils::format_command("git -C {} rev-parse HEAD 2>/dev/null", safe_project);
	std::string git_head = fs_utils::execute_command_sync(git_cmd, 5);
	size_t nl = git_head.find_first_of("\r\n");
	if (nl != std::string::npos) {
		git_head = git_head.substr(0, nl);
	}

	// Load valid blob hashes for this git commit to filter out historical duplicates
	(void)load_valid_blobs_from_git(safe_project, git_head.empty() ? "HEAD" : git_head);

	// 2. Create scratch directory for temporary JSON dumps
	auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
	std::string temp_dir = fs_utils::get_project_tmp_dir() + "/semcode_build_" + std::to_string(timestamp);
	std::filesystem::create_directories(temp_dir, ec);

	std::string fn_tmp = temp_dir + "/functions.json";
	std::string ty_tmp = temp_dir + "/types.json";

	// RAII cleanup guard to ensure temporary dumps are deleted upon return
	struct cleanup_guard {
		std::string dir;
		~cleanup_guard()
		{
			if (!dir.empty()) {
				std::error_code err;
				std::filesystem::remove_all(dir, err);
			}
		}
	} guard{temp_dir};

	// 3. Run semcode dump-functions
	std::string dump_fn_cmd =
	    fs_utils::format_command("{} -d {} --git-repo {} -q \"dump-functions {}\"", semcode_bin, safe_project, safe_project, fn_tmp);
	fs_utils::execute_command_sync(dump_fn_cmd, 120);

	// 4. Run semcode dump-types
	std::string dump_ty_cmd =
	    fs_utils::format_command("{} -d {} --git-repo {} -q \"dump-types {}\"", semcode_bin, safe_project, safe_project, ty_tmp);
	fs_utils::execute_command_sync(dump_ty_cmd, 120);

	// Ensure destination database is clean
	close();
	std::filesystem::remove(db_path_, ec);

	if (!open()) {
		return false;
	}

	// 5. Ingest dumps
	size_t func_count = 0;
	if (fs_utils::is_regular_file(fn_tmp)) {
		func_count = ingest_functions_file(fn_tmp);
	}

	size_t type_count = 0;
	if (fs_utils::is_regular_file(ty_tmp)) {
		type_count = ingest_types_file(ty_tmp);
	}

	if (func_count == 0 && type_count == 0) {
		close();
		std::filesystem::remove(db_path_, ec);
		return false;
	}

	// 6. Build indexes & save metadata
	if (!build_indices()) {
		close();
		return false;
	}

	if (!git_head.empty()) {
		(void)set_metadata("git_head", git_head);
	}
	(void)set_metadata("functions_count", std::to_string(func_count));
	(void)set_metadata("types_count", std::to_string(type_count));
	(void)set_metadata("created_at", std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));

	close();

	// 7. Prune cache directory
	if (max_dbs > 0) {
		std::filesystem::path db_file_path(db_path_);
		std::filesystem::path parent = db_file_path.parent_path();
		if (parent.empty()) {
			parent = std::filesystem::current_path();
		}
		prune_cache_directory(parent.string(), max_dbs);
	}

	return true;
}
