#include "highlighter/verilog_highlighter.h"
#include <filesystem>
#include <vector>
#include <ctre.hpp>
#include "utf8.h"

bool verilog_highlighter::supports_file(const std::string &filename) const
{
	if (filename.empty())
		return false;
	std::filesystem::path p(filename);
	std::string ext = utf8::to_lower(p.extension().string());
	return ext == ".v" || ext == ".sv" || ext == ".vh" || ext == ".svh";
}

bool verilog_highlighter::supports_language(const std::string &lang) const
{
	std::string l = utf8::to_lower(lang);
	return l == "verilog" || l == "v" || l == "systemverilog" || l == "sv";
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

	// 1. Keywords (Verilog & SystemVerilog IEEE 1800)
	for (auto match : ctre::search_all<R"(\b(module|endmodule|input|output|inout|wire|reg|logic|bit|byte|shortint|int|longint|integer|time|shortreal|real|chandle|string|event|always|always_comb|always_ff|always_latch|assign|alias|bind|begin|end|parameter|localparam|initial|final|if|else|case|default|endcase|casex|casez|generate|endgenerate|function|endfunction|task|endtask|genvar|typedef|struct|enum|union|package|endpackage|import|export|interface|endinterface|modport|clocking|endclocking|program|endprogram|class|endclass|extends|implements|virtual|local|protected|fork|join|join_any|join_none|automatic|ref|const|static|extern|pure|packed|unsigned|signed|void|new|null|this|super|return|break|continue|assert|property|endproperty|sequence|endsequence|checker|endchecker|cover|assume|disable|iff|forever|repeat|while|for|inside|unique|unique0|priority|rand|randc|constraint|covergroup|endgroup|coverpoint|cross|bins|binsof|intersect|dist|matches|tagged|wait|wait_order)\b)">(text)) {
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
	}

	// 2. System Tasks and Compiler Directives (`include, $display, etc.)
	for (auto match : ctre::search_all<R"((\$[a-zA-Z_]\w*|`[a-zA-Z_]\w*))">(text)) {
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::keyword;
			}
		}
	}

	// 3. Double-quoted Strings
	for (auto match : ctre::search_all<R"(("[^"\\]*(?:\\.[^"\\]*)*"))">(text)) {
		int char_pos = byte_to_char(match.data() - text.data());
		int char_end = byte_to_char(match.data() - text.data() + match.size());

		for (int j = char_pos; j < char_end; ++j) {
			if (j < static_cast<int>(attrs.size())) {
				attrs[j] = syntax_attribute::string_literal;
			}
		}
	}

	// 4. Block Comments (single line only: /* ... */)
	for (auto match : ctre::search_all<R"((/\*.*?\*/))">(text)) {
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

	l->set_attributes(attrs);
}
