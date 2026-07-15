#include "highlighter/verilog_highlighter.h"
#include <filesystem>
#include <vector>
#include "utf8.h"

bool verilog_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = p.extension().string();
	return ext == ".v" || ext == ".sv" || ext == ".vh" || ext == ".svh";
}

bool verilog_highlighter::supports_language(const std::string &lang) const
{
	return lang == "verilog" || lang == "v";
}

void verilog_highlighter::highlight(std::shared_ptr<line> l)
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

	// 1. Keywords (Verilog & SystemVerilog)
	static const std::unique_ptr<re2::RE2> kw_regex = std::make_unique<re2::RE2>(
		"\\b(module|endmodule|input|output|inout|wire|reg|logic|always|always_comb|always_ff|always_latch|"
		"assign|begin|end|parameter|localparam|initial|if|else|case|default|endcase|generate|endgenerate|"
		"function|endfunction|task|endtask|integer|genvar|real|time|typedef|struct|enum|package|endpackage|"
		"import|export|interface|endinterface|class|endclass|fork|join|join_any|join_none|automatic|ref|"
		"const|virtual|static|extern|pure|packed|unsigned|signed|void|new|null|this|super|assert|property|"
		"endproperty|sequence|endsequence|cover|assume|disable|forever|repeat|while|for|inside|unique|"
		"priority|casex|casez)\\b");

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

	// 3. Block Comments (single line only: /* ... */)
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

	// 4. Line Comments (//)
	size_t comment_pos = text.find("//");
	if (comment_pos != std::string::npos) {
		int char_comment_pos = byte_to_char(comment_pos);
		// Check that we aren't already inside a string or block comment
		if (char_comment_pos < static_cast<int>(attrs.size()) &&
			attrs[char_comment_pos] != syntax_attribute::string_literal &&
			attrs[char_comment_pos] != syntax_attribute::comment) {
			for (size_t i = char_comment_pos; i < attrs.size(); ++i) {
				attrs[i] = syntax_attribute::comment;
			}
		}
	}

	l->set_attributes(attrs);
}
