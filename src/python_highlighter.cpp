#include "python_highlighter.h"
#include <filesystem>
#include <vector>
#include "utf8.h"

bool python_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	return ext == ".py" || ext == ".pyw";
}

void python_highlighter::highlight(std::shared_ptr<line> l)
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

	// 1. Keywords
	static const std::unique_ptr<re2::RE2> kw_regex = std::make_unique<re2::RE2>(
	    "\\b(False|None|True|and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|"
	    "in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield)\\b");

	re2::StringPiece input(text);
	re2::StringPiece match;
	size_t search_start = 0;
	while (kw_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
	}

	// 2. Strings (single line)
	static const std::unique_ptr<re2::RE2> str_regex =
	    std::make_unique<re2::RE2>("(\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"|'[^'\\\\]*(?:\\\\.[^'\\\\]*)*')");
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

	// 3. Comments
	size_t comment_pos = text.find('#');
	if (comment_pos != std::string::npos) {
		int char_comment_pos = byte_to_char(comment_pos);
		// Only mark as comment if not already inside a string
		if (attrs[char_comment_pos] != syntax_attribute::string_literal) {
			for (size_t i = char_comment_pos; i < attrs.size(); ++i) {
				attrs[i] = syntax_attribute::comment;
			}
		}
	}

	// 4. Trailing whitespace
	/*
	int trailing_spaces = 0;
	for (int i = static_cast<int>(text.length()) - 1; i >= 0; --i) {
		if (text[i] == ' ' || text[i] == '\t') {
			trailing_spaces++;
		} else {
			break;
		}
	}
	for (int i = 0; i < trailing_spaces; ++i) {
		if (attrs.size() > static_cast<size_t>(i)) {
			attrs[attrs.size() - 1 - i] = syntax_attribute::trailing_space;
		}
	}
	*/

	l->set_attributes(attrs);
}
