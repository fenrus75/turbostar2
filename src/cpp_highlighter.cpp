#include "cpp_highlighter.h"
#include <filesystem>
#include <vector>
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

	if (text.empty()) {
		l->set_attributes(attrs);
		return;
	}

	// Helper to convert byte offset to char offset
	auto byte_to_char = [&](size_t byte_pos) -> int { return static_cast<int>(utf8::byte_to_char_pos(text, byte_pos)); };

	re2::StringPiece input(text);
	re2::StringPiece match;
	size_t search_start = 0;

	// 1. Preprocessor Directives
	static const std::unique_ptr<re2::RE2> preproc_regex = std::make_unique<re2::RE2>("^\\s*(#[a-zA-Z]+)");
	re2::StringPiece preproc_matches[2];
	if (preproc_regex->Match(input, 0, input.size(), re2::RE2::UNANCHORED, preproc_matches, 2)) {
		int char_pos = byte_to_char(preproc_matches[1].data() - text.data());
		int char_end = byte_to_char(preproc_matches[1].data() - text.data() + preproc_matches[1].size());
		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
	}

	// 2. Double-quoted Strings
	static const std::unique_ptr<re2::RE2> str_regex =
		std::make_unique<re2::RE2>("(\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\")");
	search_start = 0;
	while (str_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::string_literal;
			}
		}
	}

	// 3. Single-quoted Characters
	static const std::unique_ptr<re2::RE2> char_regex =
		std::make_unique<re2::RE2>("('[^'\\\\]*(?:\\\\.[^'\\\\]*)*')");
	search_start = 0;
	while (char_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::string_literal;
			}
		}
	}

	// 4. Block Comments (single line only: /* ... */)
	static const std::unique_ptr<re2::RE2> block_comment_regex =
		std::make_unique<re2::RE2>("(/\\*.*?\\*/)");
	search_start = 0;
	while (block_comment_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::comment;
			}
		}
	}

	// 5. Line Comments (//)
	size_t comment_pos = text.find("//");
	if (comment_pos != std::string::npos) {
		int char_comment_pos = byte_to_char(comment_pos);
		if (char_comment_pos < static_cast<int>(attrs.size()) &&
			attrs[char_comment_pos] != syntax_attribute::string_literal &&
			attrs[char_comment_pos] != syntax_attribute::comment) {
			for (size_t i = char_comment_pos; i < attrs.size(); ++i) {
				attrs[i] = syntax_attribute::comment;
			}
		}
	}

	// 6. Keywords (only where attributes are currently 'normal')
	static const std::unique_ptr<re2::RE2> kw_regex = std::make_unique<re2::RE2>(
		"\\b("
		"int|char|bool|float|double|void|auto|size_t|ssize_t|wchar_t|char8_t|char16_t|char32_t|"
		"int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|"
		"std|string|string_view|vector|map|unordered_map|set|unordered_set|optional|variant|any|tuple|pair|unique_ptr|shared_ptr|weak_ptr|"
		"const|constexpr|consteval|constinit|volatile|mutable|static|extern|inline|virtual|override|final|explicit|friend|"
		"public|protected|private|signed|unsigned|short|long|"
		"if|else|switch|case|default|for|while|do|break|continue|return|goto|try|catch|throw|noexcept|"
		"class|struct|union|enum|namespace|template|typename|concept|requires|typedef|using|"
		"new|delete|sizeof|decltype|alignof|alignas|typeid|and|and_eq|bitand|bitor|compl|not|not_eq|or|or_eq|xor|xor_eq|"
		"this|true|false|nullptr|static_assert|static_cast|dynamic_cast|const_cast|reinterpret_cast|"
		"import|module|export|co_await|co_return|co_yield"
		")\\b");

	search_start = 0;
	while (kw_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		if (match.size() == 0) {
			search_start++;
		}
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		// Only apply keyword attributes if the text region was normal code (not inside comment/string)
		bool is_safe = true;
		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size()) && attrs[j] != syntax_attribute::normal) {
				is_safe = false;
				break;
			}
		}

		if (is_safe) {
			for (int j = char_pos; j < char_end; ++j) {
				if (j < static_cast<int>(attrs.size())) {
					attrs[j] = syntax_attribute::keyword;
				}
			}
		}
	}

	l->set_attributes(attrs);
}
