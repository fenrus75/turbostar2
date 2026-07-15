#include "highlighter/json_highlighter.h"
#include <filesystem>
#include <vector>
#include "utf8.h"

bool json_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	return ext == ".json";
}

bool json_highlighter::supports_language(const std::string &lang) const
{
	return lang == "json";
}

void json_highlighter::highlight(std::shared_ptr<line> l)
{
	std::string text = l->get_text();
	size_t char_count = l->length_in_chars();
	std::vector<syntax_attribute> attrs(char_count, syntax_attribute::normal);

	if (text.empty()) {
		l->set_attributes(attrs);
		return;
	}

	auto byte_to_char = [&](size_t byte_pos) -> int {
		return static_cast<int>(utf8::byte_to_char_pos(text, byte_pos));
	};

	re2::StringPiece input(text);
	re2::StringPiece match;
	re2::StringPiece submatch;
	size_t search_start = 0;

	// 1. Comments (standard single line comments: // ...)
	size_t comment_pos = text.find("//");
	if (comment_pos != std::string::npos) {
		int char_comment_pos = byte_to_char(comment_pos);
		for (size_t i = char_comment_pos; i < attrs.size(); ++i) {
			attrs[i] = syntax_attribute::comment;
		}
	}

	// 2. JSON Keys: ("key")\s*:
	// We capture the key string as group 1, and the colon is part of the full match.
	static const std::unique_ptr<re2::RE2> key_regex = std::make_unique<re2::RE2>("(\"([^\"\\\\]|\\\\.)*\")\\s*:");
	re2::StringPiece matches[3];
	search_start = 0;
	while (key_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, matches, 3)) {
		// matches[0] contains the full match: "key" :
		// matches[1] contains the first capture group: "key"
		size_t key_start_byte = matches[1].data() - text.data();
		size_t key_len = matches[1].size();
		
		int char_pos = byte_to_char(key_start_byte);
		int char_end = byte_to_char(key_start_byte + key_len);

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size()) && attrs[j] == syntax_attribute::normal) {
				attrs[j] = syntax_attribute::keyword; // Keys are highlighted as keywords
			}
		}

		// Also highlight the colon (the last char of the match) as heading
		size_t colon_byte = matches[0].data() - text.data() + matches[0].size() - 1;
		int colon_char = byte_to_char(colon_byte);
		if (colon_char < static_cast<int>(attrs.size()) && attrs[colon_char] == syntax_attribute::normal) {
			attrs[colon_char] = syntax_attribute::heading;
		}

		search_start = (matches[0].data() - input.data()) + matches[0].size();
	}

	// 3. JSON String Values: any double-quoted string not already colored (i.e. not a key)
	static const std::unique_ptr<re2::RE2> str_regex = std::make_unique<re2::RE2>("\"([^\"\\\\]|\\\\.)*\"");
	search_start = 0;
	while (str_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		size_t start_byte = match.data() - text.data();
		int char_pos = byte_to_char(start_byte);
		int char_end = byte_to_char(start_byte + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size()) && attrs[j] == syntax_attribute::normal) {
				attrs[j] = syntax_attribute::string_literal;
			}
		}
		search_start = (match.data() - input.data()) + match.size();
	}

	// 4. Booleans and Null
	static const std::unique_ptr<re2::RE2> val_regex = std::make_unique<re2::RE2>("\\b(true|false|null)\\b");
	search_start = 0;
	while (val_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		size_t start_byte = match.data() - text.data();
		int char_pos = byte_to_char(start_byte);
		int char_end = byte_to_char(start_byte + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size()) && attrs[j] == syntax_attribute::normal) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
		search_start = (match.data() - input.data()) + match.size();
	}

	// 5. Numbers
	static const std::unique_ptr<re2::RE2> num_regex = std::make_unique<re2::RE2>("-?\\b(\\d+(\\.\\d+)?([eE][+-]?\\d+)?)\\b");
	search_start = 0;
	while (num_regex->Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
		size_t start_byte = match.data() - text.data();
		int char_pos = byte_to_char(start_byte);
		int char_end = byte_to_char(start_byte + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size()) && attrs[j] == syntax_attribute::normal) {
				attrs[j] = syntax_attribute::italic; // Numbers are highlighted as italic (dark cyan)
			}
		}
		search_start = (match.data() - input.data()) + match.size();
	}

	// 6. Syntactic Brackets and Punctuation ( { } [ ] , )
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == '{' || c == '}' || c == '[' || c == ']' || c == ',') {
			int char_idx = byte_to_char(i);
			if (char_idx < static_cast<int>(attrs.size()) && attrs[char_idx] == syntax_attribute::normal) {
				attrs[char_idx] = syntax_attribute::heading; // Brackets are highlighted as heading (cyan)
			}
		}
	}

	l->set_attributes(attrs);
}
