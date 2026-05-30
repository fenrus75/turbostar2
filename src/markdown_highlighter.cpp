#include "markdown_highlighter.h"
#include <filesystem>
#include <memory>
#include <re2/re2.h>
#include <algorithm>

static size_t byte_offset_to_char_pos(const std::string &text, size_t byte_offset)
{
	size_t char_pos = 0;
	size_t current_byte = 0;
	while (current_byte < byte_offset && current_byte < text.length()) {
		unsigned char c = static_cast<unsigned char>(text[current_byte]);
		if (c < 0x80)
			current_byte += 1;
		else if ((c & 0xE0) == 0xC0)
			current_byte += 2;
		else if ((c & 0xF0) == 0xE0)
			current_byte += 3;
		else if ((c & 0xF8) == 0xF0)
			current_byte += 4;
		else
			current_byte += 1;
		char_pos++;
	}
	return char_pos;
}

static size_t byte_len_to_char_len(const std::string &text, size_t byte_start, size_t byte_len)
{
	size_t char_len = 0;
	size_t current_byte = byte_start;
	size_t byte_end = byte_start + byte_len;
	while (current_byte < byte_end && current_byte < text.length()) {
		unsigned char c = static_cast<unsigned char>(text[current_byte]);
		if (c < 0x80)
			current_byte += 1;
		else if ((c & 0xE0) == 0xC0)
			current_byte += 2;
		else if ((c & 0xF0) == 0xE0)
			current_byte += 3;
		else if ((c & 0xF8) == 0xF0)
			current_byte += 4;
		else
			current_byte += 1;
		char_len++;
	}
	return char_len;
}

bool markdown_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
	return ext == ".md" || ext == ".markdown";
}

void markdown_highlighter::highlight(std::shared_ptr<line> l)
{
	std::string text = l->get_text();
	size_t char_count = l->length_in_chars();
	std::vector<syntax_attribute> attrs(char_count, syntax_attribute::normal);

	if (text.empty()) {
		l->set_attributes(attrs);
		return;
	}

	// Basic Heading support (starts with 1-6 '#' followed by space or end of line)
	size_t heading_chars = 0;
	while (heading_chars < text.length() && text[heading_chars] == '#') {
		heading_chars++;
	}

	bool is_heading = (heading_chars >= 1 && heading_chars <= 6) &&
	                  (heading_chars == text.length() || text[heading_chars] == ' ');

	if (is_heading) {
		for (size_t i = 0; i < heading_chars && i < char_count; ++i) {
			attrs[i] = syntax_attribute::heading;
		}
	} else {
		// Basic bold support (**...**)
		static const re2::RE2 bold_regex("\\*\\*(?:[^*]|\\*[^*]+)+\\*\\*");
		if (bold_regex.ok()) {
			re2::StringPiece input(text);
			re2::StringPiece match;
			size_t search_start = 0;
			while (bold_regex.Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
				search_start = (match.data() - input.data()) + match.size();
				if (match.size() == 0)
					search_start++;
				size_t byte_pos = match.data() - text.data();
				size_t byte_len = match.length();

				size_t char_pos = byte_offset_to_char_pos(text, byte_pos);
				size_t char_len = byte_len_to_char_len(text, byte_pos, byte_len);

				// Mark as bold
				for (size_t j = 0; j < char_len; ++j) {
					if (char_pos + j < attrs.size()) {
						attrs[char_pos + j] = syntax_attribute::bold;
					}
				}
			}
		}

		// List items (- or *) at start of line followed by space
		size_t first_non_space = text.find_first_not_of(" \t");
		if (first_non_space != std::string::npos && (text[first_non_space] == '-' || text[first_non_space] == '*')) {
			if (first_non_space + 1 < text.length() && text[first_non_space + 1] == ' ') {
				size_t char_pos = byte_offset_to_char_pos(text, first_non_space);
				if (char_pos < attrs.size()) {
					attrs[char_pos] = syntax_attribute::list_item;
				}
			}
		}
	}

	l->set_attributes(attrs);
}
