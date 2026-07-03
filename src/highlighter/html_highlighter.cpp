#include "highlighter/html_highlighter.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include "utf8.h"

bool html_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
	return ext == ".html" || ext == ".htm";
}

void html_highlighter::highlight(std::shared_ptr<line> l)
{
	std::string text = l->get_text();
	size_t char_count = l->length_in_chars();
	std::vector<syntax_attribute> attrs(char_count, syntax_attribute::normal);

	if (text.empty()) {
		l->set_attributes(attrs);
		return;
	}

	auto byte_to_char = [&](size_t byte_pos) -> int {
		return static_cast<int>(utf8::byte_to_char_pos(text, byte_pos));
	};

	enum class State {
		normal,
		in_tag,
		in_double_quote,
		in_single_quote,
		in_comment
	};

	State state = State::normal;
	size_t i = 0;
	size_t len = text.length();

	while (i < len) {
		if (state == State::normal) {
			if (i + 4 <= len && text.compare(i, 4, "<!--") == 0) {
				state = State::in_comment;
				for (int step = 0; step < 4; ++step) {
					int c_idx = byte_to_char(i + step);
					if (c_idx < static_cast<int>(attrs.size())) {
						attrs[c_idx] = syntax_attribute::comment;
					}
				}
				i += 4;
				continue;
			} else if (text[i] == '<') {
				state = State::in_tag;
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::keyword;
				}
				i++;
				continue;
			} else {
				i++;
			}
		} else if (state == State::in_comment) {
			if (i + 3 <= len && text.compare(i, 3, "-->") == 0) {
				for (int step = 0; step < 3; ++step) {
					int c_idx = byte_to_char(i + step);
					if (c_idx < static_cast<int>(attrs.size())) {
						attrs[c_idx] = syntax_attribute::comment;
					}
				}
				state = State::normal;
				i += 3;
				continue;
			} else {
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::comment;
				}
				size_t char_len = utf8::char_len(static_cast<unsigned char>(text[i]));
				i += (char_len > 0 ? char_len : 1);
			}
		} else if (state == State::in_tag) {
			if (text[i] == '>') {
				state = State::normal;
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::keyword;
				}
				i++;
				continue;
			} else if (text[i] == '"') {
				state = State::in_double_quote;
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::string_literal;
				}
				i++;
				continue;
			} else if (text[i] == '\'') {
				state = State::in_single_quote;
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::string_literal;
				}
				i++;
				continue;
			} else {
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::keyword;
				}
				size_t char_len = utf8::char_len(static_cast<unsigned char>(text[i]));
				i += (char_len > 0 ? char_len : 1);
			}
		} else if (state == State::in_double_quote) {
			if (text[i] == '"') {
				state = State::in_tag;
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::string_literal;
				}
				i++;
				continue;
			} else {
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::string_literal;
				}
				size_t char_len = utf8::char_len(static_cast<unsigned char>(text[i]));
				i += (char_len > 0 ? char_len : 1);
			}
		} else if (state == State::in_single_quote) {
			if (text[i] == '\'') {
				state = State::in_tag;
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::string_literal;
				}
				i++;
				continue;
			} else {
				int char_pos = byte_to_char(i);
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::string_literal;
				}
				size_t char_len = utf8::char_len(static_cast<unsigned char>(text[i]));
				i += (char_len > 0 ? char_len : 1);
			}
		}
	}

	l->set_attributes(attrs);
}
