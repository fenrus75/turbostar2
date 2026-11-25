#include "languages/verilog/balance_checker.h"

#include <string_view>

namespace languages::verilog
{

std::vector<languages::balance_entry> balance_checker::calculate_balance(std::span<const std::string> lines, int start_line_0,
									 int end_line_0)
{
	int begin_end = 0;
	int case_endcase = 0;
	int module_endmodule = 0;
	int curly_braces = 0;
	bool in_block_comment = false;

	auto is_ident_start = [](char c) noexcept {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	};
	auto is_ident_char = [](char c) noexcept {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
	};

	for (int i = start_line_0; i <= end_line_0 && i < static_cast<int>(lines.size()); ++i) {
		const std::string &line = lines[static_cast<size_t>(i)];
		const size_t len = line.length();
		size_t j = 0;
		bool in_string = false;

		while (j < len) {
			if (in_block_comment) {
				if (j + 1 < len && line[j] == '*' && line[j + 1] == '/') {
					in_block_comment = false;
					j += 2;
				} else {
					j++;
				}
				continue;
			}

			if (in_string) {
				if (line[j] == '\\' && j + 1 < len) {
					j += 2;
				} else if (line[j] == '"') {
					in_string = false;
					j++;
				} else {
					j++;
				}
				continue;
			}

			if (j + 1 < len && line[j] == '/' && line[j + 1] == '/') {
				break;
			}
			if (j + 1 < len && line[j] == '/' && line[j + 1] == '*') {
				in_block_comment = true;
				j += 2;
				continue;
			}

			// Verilog strings use double quotes only; single quotes belong to sized literals (1'b0, 8'hFF, '0)
			if (line[j] == '"') {
				in_string = true;
				j++;
				continue;
			}

			// Escaped Verilog identifier (\foo ) runs until whitespace
			if (line[j] == '\\') {
				j++;
				while (j < len && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') {
					j++;
				}
				continue;
			}

			if (line[j] == '{') {
				curly_braces++;
				j++;
				continue;
			}
			if (line[j] == '}') {
				curly_braces--;
				j++;
				continue;
			}

			if (is_ident_start(line[j])) {
				// Ignore member/port connections like `.end(sig)` or system tasks
				bool preceded_by_dot_or_ident = (j > 0 && (line[j - 1] == '.' || is_ident_char(line[j - 1])));
				size_t start = j;
				while (j < len && is_ident_char(line[j])) {
					j++;
				}
				if (!preceded_by_dot_or_ident) {
					std::string_view tok(line.data() + start, j - start);
					if (tok == "begin") {
						begin_end++;
					} else if (tok == "end") {
						begin_end--;
					} else if (tok == "case" || tok == "casex" || tok == "casez") {
						case_endcase++;
					} else if (tok == "endcase") {
						case_endcase--;
					} else if (tok == "module" || tok == "macromodule") {
						module_endmodule++;
					} else if (tok == "endmodule") {
						module_endmodule--;
					}
				}
				continue;
			}

			j++;
		}
	}

	return {
	    {"begin/end", "end", begin_end},
	    {"case/endcase", "endcase", case_endcase},
	    {"module/endmodule", "endmodule", module_endmodule},
	    {"braces", "}", curly_braces},
	};
}

} // namespace languages::verilog
