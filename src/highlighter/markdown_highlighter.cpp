#include "highlighter/markdown_highlighter.h"
#include <filesystem>
#include <memory>
#include <ctre.hpp>
#include <algorithm>
#include "markdown_utils.h"
#include "utf8.h"
bool markdown_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = utf8::to_lower(p.extension().string());
	return ext == ".md" || ext == ".markdown";
}

bool markdown_highlighter::supports_language(const std::string &lang) const
{
	return lang == "markdown" || lang == "md";
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
		for (auto match : ctre::search_all<R"(\*\*(?:[^*]|\*[^*]+)+\*\*)">(text)) {
			size_t byte_pos = match.data() - text.data();
			size_t byte_len = match.size();

			size_t char_pos = utf8::length(text.substr(0, byte_pos));
			size_t char_len = utf8::length(text.substr(byte_pos, byte_len));

			// Mark as bold
			for (size_t j = 0; j < char_len; ++j) {
				if (char_pos + j < attrs.size()) {
					attrs[char_pos + j] = syntax_attribute::bold;
				}
			}
		}

		// List items (- or *) at start of line followed by space
		size_t first_non_space = text.find_first_not_of(" \t");
		if (first_non_space != std::string::npos && (text[first_non_space] == '-' || text[first_non_space] == '*')) {
			if (first_non_space + 1 < text.length() && text[first_non_space + 1] == ' ') {
				size_t char_pos = utf8::length(text.substr(0, first_non_space));
				if (char_pos < attrs.size()) {
					attrs[char_pos] = syntax_attribute::list_item;
				}
			}
		}
	}

	l->set_attributes(attrs);
}
