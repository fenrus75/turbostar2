#include "languages/cpp/highlighter.h"
#include <memory>
#include <re2/re2.h>
#include <re2/stringpiece.h>
#include <vector>
#include "languages/cpp/keywords.h"
#include "languages/cpp/language.h"
#include "utf8.h"

namespace languages::cpp
{

bool highlighter::supports_file(const std::string &filename) const
{
	return languages::cpp::supports_file(filename);
}

bool highlighter::supports_language(const std::string &lang) const
{
	return languages::cpp::supports_language(lang);
}

void highlighter::highlight(std::shared_ptr<line> l)
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
	size_t comment_pos = 0;
	size_t search_pos = 0;
	while ((comment_pos = text.find("//", search_pos)) != std::string::npos) {
		int char_comment_pos = byte_to_char(comment_pos);
		if (char_comment_pos >= 0 && char_comment_pos < static_cast<int>(attrs.size())) {
			if (attrs[char_comment_pos] != syntax_attribute::string_literal &&
			    attrs[char_comment_pos] != syntax_attribute::comment) {
				for (size_t i = static_cast<size_t>(char_comment_pos); i < attrs.size(); ++i) {
					attrs[i] = syntax_attribute::comment;
				}
				break;
			}
		}
		search_pos = comment_pos + 2;
	}

	// 6. Keywords (only where attributes are currently 'normal')
	// Matches word tokens and verifies membership against centralized languages::cpp::keywords
	static const std::unique_ptr<re2::RE2> word_regex = std::make_unique<re2::RE2>("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");
	search_start = 0;
	while (word_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		search_start = (match.data() - input.data()) + match.size();
		if (match.empty()) {
			search_start++;
		}
		std::string_view word(match.data(), match.size());
		if (!languages::cpp::keywords::is_keyword(word) &&
		    !languages::cpp::keywords::is_primitive_type(word) &&
		    !languages::cpp::keywords::is_std_type_or_container(word)) {
			continue;
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

} // namespace languages::cpp
