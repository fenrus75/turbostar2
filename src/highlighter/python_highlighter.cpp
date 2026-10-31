#include "highlighter/python_highlighter.h"
#include <filesystem>
#include <vector>
#include <ctre.hpp>
#include "utf8.h"

bool python_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	return ext == ".py" || ext == ".pyw";
}

bool python_highlighter::supports_language(const std::string &lang) const
{
	return lang == "python" || lang == "py";
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
	for (auto match : ctre::search_all<R"(\b(False|None|True|and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield)\b)">(text)) {
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
	}

	// 2. Strings (single line)
	for (auto match : ctre::search_all<R"(("[^"\\]*(?:\\.[^"\\]*)*"|'[^'\\]*(?:\\.[^'\\]*)*'))">(text)) {
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::string_literal;
			}
		}
	}

	// 3. Comments
	size_t comment_pos = 0;
	size_t search_pos = 0;
	while ((comment_pos = text.find('#', search_pos)) != std::string::npos) {
		int char_comment_pos = byte_to_char(comment_pos);
		if (char_comment_pos >= 0 && char_comment_pos < static_cast<int>(attrs.size())) {
			if (attrs[char_comment_pos] != syntax_attribute::string_literal) {
				for (size_t i = static_cast<size_t>(char_comment_pos); i < attrs.size(); ++i) {
					attrs[i] = syntax_attribute::comment;
				}
				break;
			}
		}
		search_pos = comment_pos + 1;
	}

	l->set_attributes(attrs);
}
