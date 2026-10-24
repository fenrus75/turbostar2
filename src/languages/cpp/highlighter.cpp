#include "languages/cpp/highlighter.h"

#include <cctype>
#include <memory>
#include <vector>
#include "languages/cpp/keywords.h"
#include "languages/cpp/language.h"
#include "languages/cpp/lexer.h"
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

	auto byte_to_char = [&](size_t byte_pos) -> int {
		return static_cast<int>(utf8::byte_to_char_pos(text, byte_pos));
	};

	auto tokens = lexer::tokenize(text);

	for (const auto &tok : tokens) {
		if (tok.kind == token_kind::eof) {
			break;
		}

		size_t byte_pos = static_cast<size_t>(tok.text.data() - text.data());
		size_t byte_len = tok.text.size();

		if (tok.kind == token_kind::comment) {
			int char_start = byte_to_char(byte_pos);
			int char_end = byte_to_char(byte_pos + byte_len);
			for (int j = char_start; j < char_end && j < static_cast<int>(attrs.size()); ++j) {
				attrs[j] = syntax_attribute::comment;
			}
		} else if (tok.kind == token_kind::string_literal || tok.kind == token_kind::character_literal) {
			int char_start = byte_to_char(byte_pos);
			int char_end = byte_to_char(byte_pos + byte_len);
			for (int j = char_start; j < char_end && j < static_cast<int>(attrs.size()); ++j) {
				attrs[j] = syntax_attribute::string_literal;
			}
		} else if (tok.kind == token_kind::keyword) {
			int char_start = byte_to_char(byte_pos);
			int char_end = byte_to_char(byte_pos + byte_len);
			for (int j = char_start; j < char_end && j < static_cast<int>(attrs.size()); ++j) {
				attrs[j] = syntax_attribute::keyword;
			}
		} else if (tok.kind == token_kind::identifier) {
			if (keywords::is_primitive_type(tok.text) || keywords::is_std_type_or_container(tok.text)) {
				int char_start = byte_to_char(byte_pos);
				int char_end = byte_to_char(byte_pos + byte_len);
				for (int j = char_start; j < char_end && j < static_cast<int>(attrs.size()); ++j) {
					attrs[j] = syntax_attribute::keyword;
				}
			}
		} else if (tok.kind == token_kind::preprocessor) {
			// Highlight the directive word (#include, #define, etc.)
			size_t dir_end = 1;
			while (dir_end < tok.text.size() && std::isalpha(static_cast<unsigned char>(tok.text[dir_end]))) {
				++dir_end;
			}
			int char_start = byte_to_char(byte_pos);
			int char_end = byte_to_char(byte_pos + dir_end);
			for (int j = char_start; j < char_end && j < static_cast<int>(attrs.size()); ++j) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
	}

	l->set_attributes(attrs);
}

} // namespace languages::cpp
