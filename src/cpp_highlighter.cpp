#include "cpp_highlighter.h"
#include <filesystem>
#include "utf8.h"

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
	    "std|string|string_view|vector|map|unordered_map|set|unordered_set|optional|variant|any|tuple|pair|unique_ptr|shared_ptr|weak_"
	    "ptr|"
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
		if (match.size() == 0)
			search_start++;
		size_t byte_pos = match.data() - text.data();
		size_t byte_len = match.length();

		// Convert byte pos to char pos
		int char_pos = static_cast<int>(utf8::byte_to_char_pos(text, byte_pos));

		// Mark as keyword
		for (size_t j = 0; j < byte_len; ++j) {
			if (char_pos + j < attrs.size()) {
				attrs[char_pos + j] = syntax_attribute::keyword;
			}
		}
	}

	// Trailing whitespace highlighting (Simple Backward Scan)
	// Since ' ' and '\t' are single-byte ASCII, we can safely scan the raw string backwards.
	// We also know that the attrs array maps 1:1 with characters, so we just work backwards.
	int trailing_spaces = 0;
	for (int i = static_cast<int>(text.length()) - 1; i >= 0; --i) {
		if (text[i] == ' ' || text[i] == '\t') {
			trailing_spaces++;
		} else {
			break;
		}
	}

	/*
	for (int i = 0; i < trailing_spaces; ++i) {
		if (attrs.size() > static_cast<size_t>(i)) {
			attrs[attrs.size() - 1 - i] = syntax_attribute::trailing_space;
		}
	}
	*/

	l->set_attributes(attrs);
}
