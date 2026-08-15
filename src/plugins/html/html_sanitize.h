#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include "fs_utils.h"

namespace html_sanitize
{

inline bool is_safe_url_scheme(std::string_view url)
{
	if (url.empty()) return true;
	std::string s(url);
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	if (s.starts_with("javascript:") || s.starts_with("data:") || s.starts_with("vbscript:") || s.starts_with("file:")) {
		return false;
	}
	return true;
}

inline std::string sanitize_markdown_cell(std::string_view text, size_t max_len = 250)
{
	std::string res;
	for (size_t i = 0; i < text.size() && res.size() < max_len; ++i) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == 0x1b) {
			if (i + 1 < text.size() && text[i + 1] == '[') {
				i += 2;
				while (i < text.size() && (text[i] < 0x40 || text[i] > 0x7e)) {
					i++;
				}
			}
			continue;
		}
		if (c == '|') {
			res += "&#124;";
		} else if (c == '`') {
			res += "\\`";
		} else if (c < 32 && c != '\t') {
			// strip control characters (including \r, \n)
		} else if (c == 127) {
			// strip DEL
		} else {
			res += c;
		}
	}
	return res;
}

inline std::string sanitize_link_url(std::string_view url, size_t max_len = 500)
{
	if (!is_safe_url_scheme(url)) {
		return "#unsafe-scheme-blocked";
	}
	std::string res;
	for (size_t i = 0; i < url.size() && res.size() < max_len; ++i) {
		unsigned char c = static_cast<unsigned char>(url[i]);
		if (c == '(') {
			res += "%28";
		} else if (c == ')') {
			res += "%29";
		} else if (c == ' ' || c < 32 || c == 127) {
			// strip spaces and control characters
		} else {
			res += c;
		}
	}
	return res;
}

} // namespace html_sanitize
