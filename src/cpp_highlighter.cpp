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
	static const std::unique_ptr<re2::RE2> kw_regex = std::make_unique<re2::RE2>(
	    "\\b("
	    // Types
	    "int|char|bool|float|double|void|auto|size_t|ssize_t|wchar_t|char8_t|char16_t|char32_t|"
	    "int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|"
	    "std|string|string_view|vector|map|unordered_map|set|unordered_set|optional|variant|any|tuple|pair|unique_ptr|shared_ptr|weak_ptr|"
	    // Qualifiers & Specifiers
	    "const|constexpr|consteval|constinit|volatile|mutable|static|extern|inline|virtual|override|final|explicit|friend|"
	    "public|protected|private|"
	    "signed|unsigned|short|long|"
	    // Control Flow
	    "if|else|switch|case|default|for|while|do|break|continue|return|goto|"
	    // Exceptions
	    "try|catch|throw|noexcept|"
	    // Structure & Memory
	    "class|struct|union|enum|namespace|template|typename|concept|requires|typedef|using|"
	    "new|delete|sizeof|decltype|alignof|alignas|typeid|"
	    // Operators/Keywords
	    "and|and_eq|bitand|bitor|compl|not|not_eq|or|or_eq|xor|xor_eq|"
	    "this|true|false|nullptr|static_assert|static_cast|dynamic_cast|const_cast|reinterpret_cast|"
	    // C++20/23 Modules & Coroutines
	    "import|module|export|co_await|co_return|co_yield"
	    ")\\b");

	re2::StringPiece input(text);
	re2::StringPiece match;
	size_t search_start = 0;
	while (kw_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		if (match.size() == 0) search_start++;
		size_t byte_pos = match.data() - text.data();
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
