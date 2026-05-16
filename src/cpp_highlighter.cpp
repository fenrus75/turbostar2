#include "cpp_highlighter.h"
#include <filesystem>

bool cpp_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".cc" || ext == ".cxx";
}

void cpp_highlighter::highlight(std::shared_ptr<line> l)
{
	std::string text = l->get_text();
	size_t char_count = l->length_in_chars();
	std::vector<syntax_attribute> attrs(char_count, syntax_attribute::normal);

	// Pre-compiled combined regex for efficiency
	static const std::regex kw_regex("\\b(void|int|char|const|bool|class|struct|enum|virtual|override|"
					 "return|if|else|for|while|namespace|std|auto|size_t|std::string)\\b");

	auto words_begin = std::sregex_iterator(text.begin(), text.end(), kw_regex);
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

		// Mark as keyword
		for (size_t j = 0; j < byte_len; ++j) {
			if (char_pos + j < attrs.size()) {
				attrs[char_pos + j] = syntax_attribute::keyword;
			}
		}
	}

	l->set_attributes(attrs);
}
