#include "languages/balance_checker.h"

#include "languages/verilog/balance_checker.h"
#include "mime.h"

#include <cstdlib>
#include <format>

namespace languages
{

int balance_checker::calculate_brace_balance(std::span<const std::string> lines, int start_line_0, int end_line_0)
{
	int balance = 0;
	bool in_block_comment = false;
	bool in_string = false;
	char string_quote = '\0';

	for (int i = start_line_0; i <= end_line_0 && i < static_cast<int>(lines.size()); ++i) {
		const std::string &line = lines[static_cast<size_t>(i)];
		size_t len = line.length();
		size_t j = 0;

		while (j < len) {
			if (in_block_comment) {
				if (j + 1 < len && line[j] == '*' && line[j + 1] == '/') {
					in_block_comment = false;
					j += 2;
				} else {
					j++;
				}
				continue;
			}

			if (in_string) {
				if (line[j] == '\\' && j + 1 < len) {
					j += 2;
				} else if (line[j] == string_quote) {
					in_string = false;
					j++;
				} else {
					j++;
				}
				continue;
			}

			if (j + 1 < len && line[j] == '/' && line[j + 1] == '/') {
				break;
			}
			if (j + 1 < len && line[j] == '/' && line[j + 1] == '*') {
				in_block_comment = true;
				j += 2;
				continue;
			}

			if (line[j] == '"' || line[j] == '\'') {
				in_string = true;
				string_quote = line[j];
				j++;
				continue;
			}

			if (line[j] == '{') {
				balance++;
			} else if (line[j] == '}') {
				balance--;
			}
			j++;
		}
	}
	return balance;
}

bool balance_checker::uses_generic_brace_check(std::string_view safe_path, std::span<const std::string> lines)
{
	std::string lang = mime::get_language_from_extension(safe_path);
	if (lang == "verilog" || lang == "systemverilog") {
		return false;
	}
	if (mime::uses_brace_syntax(safe_path)) {
		return true;
	}
	for (const auto &l : lines) {
		if (l.find('{') != std::string::npos || l.find('}') != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::vector<balance_entry> balance_checker::calculate_balance(std::string_view safe_path, std::span<const std::string> lines,
							      int start_line_0, int end_line_0)
{
	std::string lang = mime::get_language_from_extension(safe_path);
	if (lang == "verilog" || lang == "systemverilog") {
		return verilog::balance_checker::calculate_balance(lines, start_line_0, end_line_0);
	}
	if (!uses_generic_brace_check(safe_path, lines)) {
		return {};
	}
	return {{"braces", "}", calculate_brace_balance(lines, start_line_0, end_line_0)}};
}

std::string balance_checker::check_file_warnings(std::string_view safe_path, std::span<const std::string> before_lines,
						 std::span<const std::string> after_lines)
{
	if (before_lines.empty() || after_lines.empty()) {
		return "";
	}
	auto before = calculate_balance(safe_path, before_lines, 0, static_cast<int>(before_lines.size()) - 1);
	if (before.empty()) {
		return "";
	}
	auto after = calculate_balance(safe_path, after_lines, 0, static_cast<int>(after_lines.size()) - 1);
	if (after.size() != before.size()) {
		return "";
	}

	std::string warnings;
	for (size_t i = 0; i < before.size(); ++i) {
		int b = before[i].net_balance;
		int a = after[i].net_balance;
		if (b == 0 && a != 0) {
			if (before[i].label == "braces" && before.size() == 1) {
				if (a > 0) {
					warnings += std::format(
					    "⚠️ Warning: Edit introduced unbalanced braces at file scope (net balance: +{}, "
					    "missing {} closing '}}' brace{})\n",
					    a, a, (a == 1 ? "" : "s"));
				} else {
					int abs_bal = std::abs(a);
					warnings += std::format(
					    "⚠️ Warning: Edit introduced unbalanced braces at file scope (net balance: {}, "
					    "possible extra {} closing '}}' brace{})\n",
					    a, abs_bal, (abs_bal == 1 ? "" : "s"));
				}
			} else {
				if (a > 0) {
					warnings += std::format(
					    "⚠️ Warning: Edit introduced unbalanced {} blocks at file scope (net balance: +{}, "
					    "missing {} closing '{}')\n",
					    before[i].label, a, a, before[i].close_token);
				} else {
					int abs_bal = std::abs(a);
					warnings += std::format(
					    "⚠️ Warning: Edit introduced unbalanced {} blocks at file scope (net balance: {}, "
					    "possible extra {} closing '{}')\n",
					    before[i].label, a, abs_bal, before[i].close_token);
				}
			}
		}
	}
	return warnings;
}

bool balance_checker::supports_standalone_check(std::string_view safe_path)
{
	std::string lang = mime::get_language_from_extension(safe_path);
	if (lang == "verilog" || lang == "systemverilog") {
		return true;
	}
	return mime::uses_brace_syntax(safe_path);
}

std::string balance_checker::check_content_warnings(std::string_view safe_path, std::span<const std::string> lines)
{
	if (lines.empty() || !supports_standalone_check(safe_path)) {
		return "";
	}
	auto entries = calculate_balance(safe_path, lines, 0, static_cast<int>(lines.size()) - 1);
	if (entries.empty()) {
		return "";
	}

	std::string warnings;
	for (const auto &entry : entries) {
		int a = entry.net_balance;
		if (a == 0) {
			continue;
		}
		if (entry.label == "braces" && entries.size() == 1) {
			if (a > 0) {
				warnings += std::format(
				    "⚠️ Warning: Written file has unbalanced braces at file scope (net balance: +{}, "
				    "missing {} closing '}}' brace{})\n",
				    a, a, (a == 1 ? "" : "s"));
			} else {
				int abs_bal = std::abs(a);
				warnings += std::format(
				    "⚠️ Warning: Written file has unbalanced braces at file scope (net balance: {}, "
				    "possible extra {} closing '}}' brace{})\n",
				    a, abs_bal, (abs_bal == 1 ? "" : "s"));
			}
		} else {
			if (a > 0) {
				warnings += std::format(
				    "⚠️ Warning: Written file has unbalanced {} blocks at file scope (net balance: +{}, "
				    "missing {} closing '{}')\n",
				    entry.label, a, a, entry.close_token);
			} else {
				int abs_bal = std::abs(a);
				warnings += std::format(
				    "⚠️ Warning: Written file has unbalanced {} blocks at file scope (net balance: {}, "
				    "possible extra {} closing '{}')\n",
				    entry.label, a, abs_bal, entry.close_token);
			}
		}
	}
	return warnings;
}

std::string balance_checker::check_content_warnings(std::string_view safe_path, std::string_view content)
{
	if (content.empty() || !supports_standalone_check(safe_path)) {
		return "";
	}
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos < content.size()) {
		size_t nl = content.find('\n', pos);
		if (nl == std::string_view::npos) {
			lines.emplace_back(content.substr(pos));
			break;
		}
		lines.emplace_back(content.substr(pos, nl - pos));
		pos = nl + 1;
	}
	return check_content_warnings(safe_path, lines);
}

} // namespace languages
