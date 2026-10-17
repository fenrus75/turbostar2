#include "fs_basics.h"
#include "utf8.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <pthread.h>
#include <vector>

namespace fs_utils
{

file_type_t get_buffer_file_type(std::string_view buffer)
{
	size_t check_len = std::min<size_t>(buffer.size(), 4096);
	bool has_zero = false;
	bool has_other_control = false;

	for (size_t i = 0; i < check_len; ++i) {
		unsigned char b = static_cast<unsigned char>(buffer[i]);
		if (b == 0) {
			has_zero = true;
		} else if ((b < 32 && b != 9 && b != 10 && b != 11 && b != 12 && b != 13 && b != 27) || b == 127) {
			has_other_control = true;
		}
	}

	if (has_other_control) {
		return file_type_t::BINARY;
	}
	if (has_zero) {
		return file_type_t::MAYBE;
	}
	return file_type_t::ASCII;
}

bool is_binary_buffer(std::string_view buffer, bool treat_null_as_binary)
{
	file_type_t type = get_buffer_file_type(buffer);
	if (type == file_type_t::BINARY) {
		return true;
	}
	if (treat_null_as_binary && type == file_type_t::MAYBE) {
		return true;
	}
	return false;
}

bool is_valid_db_name(std::string_view name) noexcept
{
	if (name.empty())
		return false;
	for (char c : name) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

bool is_shell_safe(std::string_view s, bool allow_tilde) noexcept
{
	if (s.empty())
		return false;

	// Prevent directory traversal or flag injection at the start
	if (s.find("..") != std::string_view::npos || s.front() == '-') {
		return false;
	}

	// Strict allowlist: Alphanumeric, slash, dot, underscore, hyphen, equals, colon, plus, comma, at-sign.
	// Explicitly excludes: Space, quote marks, ampersand, pipe, redirect, semicolon, backtick, dollar, braces, etc.
	for (char c : s) {
		bool is_safe = std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '.' || c == '_' || c == '-' || c == '=' || c == ':' || c == '+' ||
			       c == ',' || c == '@';
		if (allow_tilde && c == '~') {
			is_safe = true;
		}
		if (!is_safe) {
			return false;
		}
	}
	return true;
}

bool is_safe_for_ui(std::string_view s) noexcept
{
	for (unsigned char c : s) {
		// Reject control characters (0-31), including ESC (27)
		// Allow TAB (9), LF (10), CR (13) if we want to be more lenient,
		// but for a status line, we should probably be strict.
		if (c < 32 || c == 127) {
			return false;
		}
	}
	return true;
}

std::string escape_shell_arg(std::string_view arg)
{
	std::string escaped;
	escaped.reserve(arg.size() + 10);
	escaped += '\'';
	for (char c : arg) {
		if (c == '\'') {
			escaped += "'\\''";
		} else {
			escaped += c;
		}
	}
	escaped += '\'';
	return escaped;
}

std::string unescape_string(std::string_view input)
{
	std::string result;
	result.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' && i + 1 < input.size()) {
			switch (input[i + 1]) {
				case 'n':
					result += '\n';
					break;
				case 'r':
					result += '\r';
					break;
				case 't':
					result += '\t';
					break;
				case '\\':
					result += '\\';
					break;
				case '"':
					result += '"';
					break;
				case '\'':
					result += '\'';
					break;
				default:
					result += '\\';
					result += input[i + 1];
					break;
			}
			++i;
		} else {
			result += input[i];
		}
	}
	return result;
}

std::string escape_json_string(std::string_view input, bool include_quotes)
{
	std::string escaped;
	escaped.reserve(input.size() + (include_quotes ? 16 : 14));
	if (include_quotes) {
		escaped += '"';
	}
	for (char c : input) {
		switch (c) {
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\b':
				escaped += "\\b";
				break;
			case '\f':
				escaped += "\\f";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					escaped += std::format("\\u{:04x}", static_cast<unsigned char>(c));
				} else {
					escaped += c;
				}
				break;
		}
	}
	if (include_quotes) {
		escaped += '"';
	}
	return escaped;
}

std::string wrap_prompt_untrusted_data_tag(std::string_view tag, std::string_view content)
{
	std::string safe_content(content);
	std::string close_tag = std::format("</{}>", tag);
	std::string escaped_close = std::format("&lt;/{}>", tag);

	size_t pos = 0;
	while ((pos = safe_content.find(close_tag, pos)) != std::string::npos) {
		safe_content.replace(pos, close_tag.length(), escaped_close);
		pos += escaped_close.length();
	}

	return std::format("<{}>\n{}\n</{}>", tag, safe_content, tag);
}

std::string unwrap_prompt_untrusted_data_tag(std::string_view content)
{
	std::string s(content);
	size_t first_tag_open = s.find('<');
	if (first_tag_open == std::string::npos) {
		return s;
	}
	size_t first_tag_close = s.find('>', first_tag_open);
	if (first_tag_close == std::string::npos) {
		return s;
	}
	std::string tag_name = s.substr(first_tag_open + 1, first_tag_close - (first_tag_open + 1));
	if (tag_name.empty() || tag_name[0] == '/') {
		return s;
	}
	std::string close_tag = std::format("</{}>", tag_name);
	size_t last_tag_open = s.rfind(close_tag);
	if (last_tag_open == std::string::npos || last_tag_open <= first_tag_close) {
		return s;
	}

	std::string inner = s.substr(first_tag_close + 1, last_tag_open - (first_tag_close + 1));
	size_t start = inner.find_first_not_of("\r\n");
	size_t end = inner.find_last_not_of("\r\n");
	if (start == std::string::npos || end == std::string::npos) {
		return "";
	}
	return inner.substr(start, end - start + 1);
}

bool is_prompt_tag_wrapped(std::string_view content) noexcept
{
	std::string_view s = utf8::trim_view(content);
	if (s.empty() || s.front() != '<') {
		return false;
	}
	size_t open_end = s.find('>');
	if (open_end == std::string_view::npos || open_end <= 1) {
		return false;
	}
	std::string_view tag_name = s.substr(1, open_end - 1);
	if (tag_name.empty() || tag_name[0] == '/' || tag_name[0] == '!' || tag_name[0] == '?') {
		return false;
	}
	std::string close_tag = std::format("</{}>", tag_name);
	if (!s.ends_with(close_tag)) {
		return false;
	}
	size_t first_close_pos = s.find(close_tag);
	return first_close_pos != std::string_view::npos && first_close_pos == s.length() - close_tag.length();
}

std::string shorten_filename(std::string_view filepath, int max_length)
{
	if (filepath.length() <= static_cast<size_t>(max_length)) {
		return std::string(filepath);
	}
	if (max_length <= 4) {
		return std::string(filepath.substr(filepath.length() - max_length));
	}

	std::filesystem::path p(filepath);
	std::string basename = p.filename().string();

	std::vector<std::string> parts;
	for (const auto &part : p) {
		parts.push_back(part.string());
	}

	if (parts.size() <= 2) {
		std::string res = "...." + basename;
		return res.substr(res.length() - max_length);
	}

	std::string first_dir = parts[0];
	int start_idx = 1;
	if (first_dir == "/" && parts.size() > 1) {
		first_dir += parts[1];
		start_idx = 2;
	}

	std::string result_right = "/" + basename;
	int remaining_space = max_length - 4 - first_dir.length() - result_right.length();

	if (remaining_space < 0) {
		std::string full(filepath);
		return std::format("....{}", full.substr(full.length() - (max_length - 4)));
	}

	int current_idx = parts.size() - 2;
	while (current_idx >= start_idx) {
		std::string to_add = std::format("/{}", parts[current_idx]);
		if (remaining_space >= static_cast<int>(to_add.length())) {
			result_right = to_add + result_right;

			remaining_space -= to_add.length();
			current_idx--;
		} else {
			break;
		}
	}

	return first_dir + "...." + result_right;
}

std::string base64_encode(std::span<const unsigned char> data)
{
	static const char trailing_char = '=';
	static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					   "abcdefghijklmnopqrstuvwxyz"
					   "0123456789+/";

	std::string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	for (unsigned char byte : data) {
		char_array_3[i++] = byte;
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while ((i++ < 3))
			ret += trailing_char;
	}

	return ret;
}

std::string base64_encode(std::string_view text)
{
	return base64_encode(std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}

std::vector<unsigned char> base64_decode(std::string_view encoded)
{
	static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
						"abcdefghijklmnopqrstuvwxyz"
						"0123456789+/";

	auto is_base64 = [](unsigned char c) -> bool { return (std::isalnum(c) || (c == '+') || (c == '/')); };

	size_t in_len = encoded.size();
	size_t i = 0;
	size_t j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	std::vector<unsigned char> ret;

	while (in_len-- && (encoded[in_] != '=') && is_base64(static_cast<unsigned char>(encoded[in_]))) {
		char_array_4[i++] = encoded[in_];
		in_++;
		if (i == 4) {
			for (i = 0; i < 4; i++) {
				char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
			}
			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++) {
				ret.push_back(char_array_3[i]);
			}
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 4; j++) {
			char_array_4[j] = 0;
		}
		for (j = 0; j < 4; j++) {
			char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));
		}
		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++) {
			ret.push_back(char_array_3[j]);
		}
	}

	return ret;
}

std::string format_binary_output(std::span<const unsigned char> data, std::string_view format, std::string_view mime_type)
{
	if (format != "hex") {
		return std::format("data:{};base64,{}", mime_type, base64_encode(data));
	}
	std::string hex_str;
	for (size_t i = 0; i < data.size(); ++i) {
		if (i > 0) {
			hex_str += " ";
		}
		hex_str += std::format("{:02x}", data[i]);
	}
	return hex_str;
}

size_t levenshtein_distance(std::string_view s1, std::string_view s2)
{
	size_t len1 = s1.length();
	size_t len2 = s2.length();
	std::vector<std::vector<size_t>> d(len1 + 1, std::vector<size_t>(len2 + 1));

	for (size_t i = 0; i <= len1; ++i) d[i][0] = i;
	for (size_t j = 0; j <= len2; ++j) d[0][j] = j;

	for (size_t i = 1; i <= len1; ++i) {
		for (size_t j = 1; j <= len2; ++j) {
			size_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
			d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
		}
	}
	return d[len1][len2];
}

std::string repair_json_string(std::string_view untrusted_json)
{
	if (untrusted_json.empty()) {
		return "{}";
	}

	try {
		auto parsed = nlohmann::json::parse(untrusted_json);
		return std::string(untrusted_json);
	} catch (...) {
	}

	std::string s(untrusted_json);
	std::vector<char> stack;
	bool in_string = false;
	bool escaped = false;

	for (char c : s) {
		if (in_string) {
			if (escaped) {
				escaped = false;
			} else if (c == '\\') {
				escaped = true;
			} else if (c == '"') {
				in_string = false;
			}
		} else {
			if (c == '"') {
				in_string = true;
			} else if (c == '{' || c == '[') {
				stack.push_back(c);
			} else if (c == '}' && !stack.empty() && stack.back() == '{') {
				stack.pop_back();
			} else if (c == ']' && !stack.empty() && stack.back() == '[') {
				stack.pop_back();
			}
		}
	}

	std::string repaired = s;
	if (in_string) {
		repaired += '"';
	}
	while (!stack.empty()) {
		char top = stack.back();
		stack.pop_back();
		repaired += (top == '{' ? '}' : ']');
	}

	try {
		auto parsed = nlohmann::json::parse(repaired);
		return repaired;
	} catch (...) {
	}

	return "{}";
}

void set_current_thread_name(std::string_view name)
{
	char buf[16];
	size_t len = std::min(name.size(), sizeof(buf) - 1);
	std::memcpy(buf, name.data(), len);
	buf[len] = '\0';
	pthread_setname_np(pthread_self(), buf);
}

std::string get_current_thread_name()
{
	char buf[16] = {0};
	(void)pthread_getname_np(pthread_self(), buf, sizeof(buf));
	return std::string(buf);
}

} // namespace fs_utils
