#pragma once

#include <string>
#include <algorithm>
#include <cctype>

namespace tools
{

inline std::string strip_sql_comments(const std::string &sql)
{
	std::string clean;
	clean.reserve(sql.size());
	bool in_single_comment = false;
	bool in_multi_comment = false;
	bool in_string = false;
	char string_quote = 0;

	for (size_t i = 0; i < sql.size(); ++i) {
		if (in_single_comment) {
			if (sql[i] == '\n' || sql[i] == '\r') {
				in_single_comment = false;
				clean += ' ';
			}
			continue;
		}
		if (in_multi_comment) {
			if (sql[i] == '*' && i + 1 < sql.size() && sql[i + 1] == '/') {
				in_multi_comment = false;
				i++;
				clean += ' ';
			}
			continue;
		}
		if (in_string) {
			clean += sql[i];
			if (sql[i] == string_quote) {
				if (i + 1 < sql.size() && sql[i + 1] == string_quote) {
					clean += sql[i + 1];
					i++;
				} else {
					in_string = false;
				}
			}
			continue;
		}

		if (sql[i] == '\'' || sql[i] == '"') {
			in_string = true;
			string_quote = sql[i];
			clean += sql[i];
			continue;
		}

		if (sql[i] == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
			in_single_comment = true;
			i++;
			continue;
		}

		if (sql[i] == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
			in_multi_comment = true;
			i++;
			continue;
		}

		clean += sql[i];
	}
	return clean;
}

inline bool validate_query_safety(const std::string &query, std::string &out_error)
{
	std::string clean_sql = strip_sql_comments(query);
	size_t start = clean_sql.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) {
		out_error = "Error: Query is empty.";
		return false;
	}
	size_t end = clean_sql.find_last_not_of(" \t\r\n");
	clean_sql = clean_sql.substr(start, end - start + 1);

	// Strip single trailing semicolon
	if (!clean_sql.empty() && clean_sql.back() == ';') {
		clean_sql.pop_back();
		size_t last_non_space = clean_sql.find_last_not_of(" \t\r\n");
		if (last_non_space != std::string::npos) {
			clean_sql = clean_sql.substr(0, last_non_space + 1);
		}
	}

	std::string upper;
	upper.reserve(clean_sql.size());
	for (char c : clean_sql) {
		upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}

	// Reject multiple statements (semicolons in body outside strings)
	bool in_str = false;
	char q = 0;
	for (size_t i = 0; i < clean_sql.size(); ++i) {
		if (in_str) {
			if (clean_sql[i] == q) in_str = false;
		} else if (clean_sql[i] == '\'' || clean_sql[i] == '"') {
			in_str = true;
			q = clean_sql[i];
		} else if (clean_sql[i] == ';') {
			out_error = "Error: Multiple SQL statements in a single execution are prohibited.";
			return false;
		}
	}

	// Reject dangerous SQL keywords anywhere
	if (upper.find("ATTACH") != std::string::npos) {
		out_error = "Error: ATTACH DATABASE statements are prohibited.";
		return false;
	}
	if (upper.find("DETACH") != std::string::npos) {
		out_error = "Error: DETACH DATABASE statements are prohibited.";
		return false;
	}
	if (upper.find("VACUUM") != std::string::npos) {
		out_error = "Error: VACUUM statements are prohibited.";
		return false;
	}

	return true;
}

} // namespace tools
