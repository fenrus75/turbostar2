#include "markdown_highlighter.h"
#include <filesystem>
#include <regex>

bool markdown_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
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

	// Basic Heading support (starts with #)
	if (text[0] == '#') {
		for (size_t i = 0; i < char_count; ++i) {
			attrs[i] = syntax_attribute::heading;
		}
	} else {
		// Basic bold support (**...**)
		static const std::regex bold_regex("\\*\\*.*?\\*\\*");
		auto words_begin = std::sregex_iterator(text.begin(), text.end(), bold_regex);
		auto words_end = std::sregex_iterator();

		for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
			std::smatch match = *i;
			size_t byte_pos = match.position();
			size_t byte_len = match.length();

			// Convert byte pos to char pos
			int char_pos = 0;
			size_t current_byte = 0;
			while (current_byte < byte_pos && char_pos < static_cast<int>(char_count)) {
				unsigned char c = static_cast<unsigned char>(text[current_byte]);
				if (c < 0x80)
					current_byte += 1;
				else if ((c & 0xE0) == 0xC0)
					current_byte += 2;
				else if ((c & 0xE0) == 0xE0)
					current_byte += 3;
				else if ((c & 0xF0) == 0xF0)
					current_byte += 4;
				else
					current_byte += 1;
				char_pos++;
			}

			// Mark as bold
			for (size_t j = 0; j < byte_len; ++j) {
				if (char_pos + j < attrs.size()) {
					attrs[char_pos + j] = syntax_attribute::bold;
				}
			}
		}
		
		// List items (- or *) at start of line
		// Just a simple check for leading space then - or *
		size_t first_non_space = text.find_first_not_of(" \t");
		if (first_non_space != std::string::npos && (text[first_non_space] == '-' || text[first_non_space] == '*')) {
			// Check if it's followed by a space to be a real list item
			if (first_non_space + 1 < text.length() && text[first_non_space + 1] == ' ') {
				// We need char position of first_non_space
				int char_pos = 0;
				size_t current_byte = 0;
				while (current_byte < first_non_space && char_pos < static_cast<int>(char_count)) {
					unsigned char c = static_cast<unsigned char>(text[current_byte]);
					if (c < 0x80)
						current_byte += 1;
					else if ((c & 0xE0) == 0xC0)
						current_byte += 2;
					else if ((c & 0xE0) == 0xE0)
						current_byte += 3;
					else if ((c & 0xF0) == 0xF0)
						current_byte += 4;
					else
						current_byte += 1;
					char_pos++;
				}
				if (char_pos < static_cast<int>(attrs.size())) {
					attrs[char_pos] = syntax_attribute::list_item;
				}
			}
		}
	}

	l->set_attributes(attrs);
}
