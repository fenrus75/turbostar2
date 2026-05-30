#include "markdown_highlighter.h"
#include <filesystem>
#include <memory>
#include <re2/re2.h>
#include <algorithm>
#include "markdown_utils.h"

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

	// Basic Heading support (starts with exactly one '#' followed by non-'#')
	bool is_heading = text[0] == '#' && (text.length() == 1 || text[1] != '#');

	if (is_heading) {
		for (size_t i = 0; i < char_count; ++i) {
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

				size_t char_pos = markdown_utils::utf8_length(text.substr(0, byte_pos));
				size_t char_len = markdown_utils::utf8_length(text.substr(byte_pos, byte_len));

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
				size_t char_pos = markdown_utils::utf8_length(text.substr(0, first_non_space));
				if (char_pos < attrs.size()) {
					attrs[char_pos] = syntax_attribute::list_item;
				}
			}
		}
	}

	l->set_attributes(attrs);
}
