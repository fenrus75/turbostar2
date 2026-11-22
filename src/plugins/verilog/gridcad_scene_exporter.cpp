#include "plugins/verilog/gridcad_scene_exporter.h"
#include "fs_utils.h"
#include "mapped_file.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/vcd_label_manager.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/yosys_netlist_reader.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace verilog {

namespace {

constexpr const char *kGridcadLiberty = R"LIB(library(gridcad) {
  cell(BUF)   { area: 1; pin(A) { direction: input; } pin(Y) { direction: output; function: "A"; } }
  cell(INV)   { area: 1; pin(A) { direction: input; } pin(Y) { direction: output; function: "!A"; } }
  cell(AND2)  { area: 2; pin(A) { direction: input; } pin(B) { direction: input; } pin(Y) { direction: output; function: "A & B"; } }
  cell(NAND2) { area: 1; pin(A) { direction: input; } pin(B) { direction: input; } pin(Y) { direction: output; function: "!(A & B)"; } }
  cell(OR2)   { area: 2; pin(A) { direction: input; } pin(B) { direction: input; } pin(Y) { direction: output; function: "A | B"; } }
  cell(NOR2)  { area: 1; pin(A) { direction: input; } pin(B) { direction: input; } pin(Y) { direction: output; function: "!(A | B)"; } }
  cell(XOR2)  { area: 3; pin(A) { direction: input; } pin(B) { direction: input; } pin(Y) { direction: output; function: "A ^ B"; } }
  cell(DFF)   { area: 4; ff(IQ, IQN) { clocked_on: "CLK"; next_state: "D"; } pin(CLK) { direction: input; clock: true; } pin(D) { direction: input; } pin(Q) { direction: output; function: "IQ"; } }
}
)LIB";

constexpr const char *kGridcadTechmap = R"TMAP(
(* blackbox *) module gridcad_reg #(parameter WIDTH = 1, parameter [WIDTH-1:0] RESET_VAL = 0) (
  input clk, input rst, input en, input [WIDTH-1:0] d, output [WIDTH-1:0] q
); endmodule
(* blackbox *) module gridcad_mux #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, input s, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_pmux #(parameter WIDTH = 1, parameter S_WIDTH = 2) (
  input [WIDTH-1:0] a, input [S_WIDTH*WIDTH-1:0] b, input [S_WIDTH-1:0] s, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_add #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_sub #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_eq #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output y
); endmodule
(* blackbox *) module gridcad_ne #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output y
); endmodule
(* blackbox *) module gridcad_gt #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output y
); endmodule
(* blackbox *) module gridcad_ge #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output y
); endmodule
(* blackbox *) module gridcad_lt #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output y
); endmodule
(* blackbox *) module gridcad_le #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output y
); endmodule
(* blackbox *) module gridcad_and #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_or #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_xor #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, input [WIDTH-1:0] b, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_not #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, output [WIDTH-1:0] y
); endmodule
(* blackbox *) module gridcad_reduce_or #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, output y
); endmodule
(* blackbox *) module gridcad_reduce_and #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, output y
); endmodule
(* blackbox *) module gridcad_reduce_xor #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, output y
); endmodule
(* blackbox *) module gridcad_reduce_xnor #(parameter WIDTH = 1) (
  input [WIDTH-1:0] a, output y
); endmodule

module \$dff (CLK, D, Q);
  parameter CLK_POLARITY = 1, WIDTH = 1;
  input CLK; input [WIDTH-1:0] D; output [WIDTH-1:0] Q;
  generate
    if (CLK_POLARITY == 1)
      gridcad_reg #(.WIDTH(WIDTH), .RESET_VAL({WIDTH{1'b0}})) _TECHMAP_REPLACE_ (.clk(CLK), .rst(1'b0), .en(1'b1), .d(D), .q(Q));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$dffe (CLK, EN, D, Q);
  parameter CLK_POLARITY = 1, EN_POLARITY = 1, WIDTH = 1;
  input CLK, EN; input [WIDTH-1:0] D; output [WIDTH-1:0] Q;
  generate
    if (CLK_POLARITY == 1 && EN_POLARITY == 1)
      gridcad_reg #(.WIDTH(WIDTH), .RESET_VAL({WIDTH{1'b0}})) _TECHMAP_REPLACE_ (.clk(CLK), .rst(1'b0), .en(EN), .d(D), .q(Q));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$adff (CLK, ARST, D, Q);
  parameter CLK_POLARITY = 1, ARST_POLARITY = 1, ARST_VALUE = 0, WIDTH = 1;
  input CLK, ARST; input [WIDTH-1:0] D; output [WIDTH-1:0] Q;
  generate
    if (CLK_POLARITY == 1)
      gridcad_reg #(.WIDTH(WIDTH), .RESET_VAL(ARST_VALUE)) _TECHMAP_REPLACE_ (.clk(CLK), .rst(ARST_POLARITY ? ARST : ~ARST), .en(1'b1), .d(D), .q(Q));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$adffe (CLK, ARST, EN, D, Q);
  parameter CLK_POLARITY = 1, ARST_POLARITY = 1, EN_POLARITY = 1, ARST_VALUE = 0, WIDTH = 1;
  input CLK, ARST, EN; input [WIDTH-1:0] D; output [WIDTH-1:0] Q;
  generate
    if (CLK_POLARITY == 1 && EN_POLARITY == 1)
      gridcad_reg #(.WIDTH(WIDTH), .RESET_VAL(ARST_VALUE)) _TECHMAP_REPLACE_ (.clk(CLK), .rst(ARST_POLARITY ? ARST : ~ARST), .en(EN), .d(D), .q(Q));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$sdff (CLK, SRST, D, Q);
  parameter CLK_POLARITY = 1, SRST_POLARITY = 1, SRST_VALUE = 0, WIDTH = 1;
  input CLK, SRST; input [WIDTH-1:0] D; output [WIDTH-1:0] Q;
  generate
    if (CLK_POLARITY == 1 && SRST_POLARITY == 1)
      gridcad_reg #(.WIDTH(WIDTH), .RESET_VAL(SRST_VALUE)) _TECHMAP_REPLACE_ (.clk(CLK), .rst(SRST), .en(1'b1), .d(D), .q(Q));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$sdffe (CLK, SRST, EN, D, Q);
  parameter CLK_POLARITY = 1, SRST_POLARITY = 1, EN_POLARITY = 1, SRST_VALUE = 0, WIDTH = 1;
  input CLK, SRST, EN; input [WIDTH-1:0] D; output [WIDTH-1:0] Q;
  generate
    if (CLK_POLARITY == 1 && SRST_POLARITY == 1 && EN_POLARITY == 1)
      gridcad_reg #(.WIDTH(WIDTH), .RESET_VAL(SRST_VALUE)) _TECHMAP_REPLACE_ (.clk(CLK), .rst(SRST), .en(EN), .d(D), .q(Q));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$mux (A, B, S, Y);
  parameter WIDTH = 1; input [WIDTH-1:0] A, B; input S; output [WIDTH-1:0] Y;
  generate
    if (WIDTH >= 1) gridcad_mux #(.WIDTH(WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .s(S), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$pmux (A, B, S, Y);
  parameter WIDTH = 1, S_WIDTH = 1;
  input [WIDTH-1:0] A; input [S_WIDTH*WIDTH-1:0] B; input [S_WIDTH-1:0] S; output [WIDTH-1:0] Y;
  generate
    if (WIDTH >= 1 && S_WIDTH >= 2)
      gridcad_pmux #(.WIDTH(WIDTH), .S_WIDTH(S_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .s(S), .y(Y));
    else if (WIDTH >= 1 && S_WIDTH == 1)
      gridcad_mux #(.WIDTH(WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .s(S), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$add (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH >= 2 && A_WIDTH == Y_WIDTH && B_WIDTH == Y_WIDTH)
      gridcad_add #(.WIDTH(Y_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$sub (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH >= 2 && A_WIDTH == Y_WIDTH && B_WIDTH == Y_WIDTH)
      gridcad_sub #(.WIDTH(Y_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$eq (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  parameter [A_WIDTH-1:0] _TECHMAP_CONSTMSK_A_ = 0;
  parameter [B_WIDTH-1:0] _TECHMAP_CONSTMSK_B_ = 0;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH == B_WIDTH && A_WIDTH >= 2 &&
        !(&_TECHMAP_CONSTMSK_A_) && !(&_TECHMAP_CONSTMSK_B_))
      gridcad_eq #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$ne (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  parameter [A_WIDTH-1:0] _TECHMAP_CONSTMSK_A_ = 0;
  parameter [B_WIDTH-1:0] _TECHMAP_CONSTMSK_B_ = 0;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH == B_WIDTH && A_WIDTH >= 2 &&
        !(&_TECHMAP_CONSTMSK_A_) && !(&_TECHMAP_CONSTMSK_B_))
      gridcad_ne #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$gt (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (A_SIGNED == 0 && B_SIGNED == 0 && Y_WIDTH == 1 && A_WIDTH == B_WIDTH && A_WIDTH >= 2)
      gridcad_gt #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$ge (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (A_SIGNED == 0 && B_SIGNED == 0 && Y_WIDTH == 1 && A_WIDTH == B_WIDTH && A_WIDTH >= 2)
      gridcad_ge #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$lt (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (A_SIGNED == 0 && B_SIGNED == 0 && Y_WIDTH == 1 && A_WIDTH == B_WIDTH && A_WIDTH >= 2)
      gridcad_lt #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$le (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (A_SIGNED == 0 && B_SIGNED == 0 && Y_WIDTH == 1 && A_WIDTH == B_WIDTH && A_WIDTH >= 2)
      gridcad_le #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$and (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH >= 2 && A_WIDTH == Y_WIDTH && B_WIDTH == Y_WIDTH)
      gridcad_and #(.WIDTH(Y_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$or (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH >= 2 && A_WIDTH == Y_WIDTH && B_WIDTH == Y_WIDTH)
      gridcad_or #(.WIDTH(Y_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$xor (A, B, Y);
  parameter A_SIGNED = 0, B_SIGNED = 0, A_WIDTH = 1, B_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; input [B_WIDTH-1:0] B; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH >= 2 && A_WIDTH == Y_WIDTH && B_WIDTH == Y_WIDTH)
      gridcad_xor #(.WIDTH(Y_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .b(B), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$not (A, Y);
  parameter A_SIGNED = 0, A_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH >= 2 && A_WIDTH == Y_WIDTH)
      gridcad_not #(.WIDTH(Y_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$reduce_or (A, Y);
  parameter A_SIGNED = 0, A_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH >= 2)
      gridcad_reduce_or #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$reduce_bool (A, Y);
  parameter A_SIGNED = 0, A_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH >= 2)
      gridcad_reduce_or #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$reduce_and (A, Y);
  parameter A_SIGNED = 0, A_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH >= 2)
      gridcad_reduce_and #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$reduce_xor (A, Y);
  parameter A_SIGNED = 0, A_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH >= 2)
      gridcad_reduce_xor #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule

module \$reduce_xnor (A, Y);
  parameter A_SIGNED = 0, A_WIDTH = 1, Y_WIDTH = 1;
  input [A_WIDTH-1:0] A; output [Y_WIDTH-1:0] Y;
  generate
    if (Y_WIDTH == 1 && A_WIDTH >= 2)
      gridcad_reduce_xnor #(.WIDTH(A_WIDTH)) _TECHMAP_REPLACE_ (.a(A), .y(Y));
    else wire _TECHMAP_FAIL_ = 1;
  endgenerate
endmodule
)TMAP";

std::string bits_key(const nlohmann::json &bits_arr)
{
	std::string k;
	for (size_t i = 0; i < bits_arr.size(); ++i) {
		if (i > 0) k.push_back(',');
		if (bits_arr[i].is_number_integer()) {
			k += std::to_string(bits_arr[i].get<int64_t>());
		} else if (bits_arr[i].is_string()) {
			k += bits_arr[i].get<std::string>();
		}
	}
	return k;
}

bool is_all_const_bits(const nlohmann::json &bits_arr)
{
	if (!bits_arr.is_array() || bits_arr.empty()) {
		return false;
	}
	for (const auto &b : bits_arr) {
		if (!b.is_string()) {
			return false;
		}
	}
	return true;
}

std::string format_const_hex(const nlohmann::json &bits_arr)
{
	uint64_t val = 0;
	for (size_t i = 0; i < bits_arr.size() && i < 64; ++i) {
		if (bits_arr[i].is_string() && bits_arr[i].get<std::string>() == "1") {
			val |= (1ULL << i);
		}
	}
	size_t hex_digits = (bits_arr.size() + 3) / 4;
	return std::format("0x{:0{}x}", val, hex_digits);
}

std::string normalize_rel_src(const fs::path &root_path, std::string_view cleaned_src)
{
	if (cleaned_src.empty()) {
		return "";
	}
	size_t colon = cleaned_src.rfind(':');
	if (colon == std::string_view::npos) {
		return std::string(cleaned_src);
	}
	std::string_view file_part = cleaned_src.substr(0, colon);
	std::string_view line_part = cleaned_src.substr(colon + 1);
	std::error_code ec;
	fs::path fp(file_part);
	if (fp.is_absolute()) {
		fs::path rel = fs::relative(fp, root_path, ec);
		if (!ec && !rel.empty() && !rel.generic_string().starts_with("..")) {
			return std::format("{}:{}", rel.generic_string(), line_part);
		}
	}
	return std::string(cleaned_src);
}

std::string read_range_src(const fs::path &root_path, std::string_view cleaned_src)
{
	if (cleaned_src.empty()) {
		return "";
	}
	size_t colon = cleaned_src.rfind(':');
	if (colon == std::string_view::npos) {
		return "";
	}
	std::string_view file_part = cleaned_src.substr(0, colon);
	std::string_view range_part = cleaned_src.substr(colon + 1);

	int start_line = 0;
	int end_line = 0;
	size_t dash = range_part.find('-');
	try {
		if (dash == std::string_view::npos) {
			start_line = std::stoi(std::string(range_part));
			end_line = start_line;
		} else {
			start_line = std::stoi(std::string(range_part.substr(0, dash)));
			end_line = std::stoi(std::string(range_part.substr(dash + 1)));
		}
	} catch (...) {
		return "";
	}
	if (start_line <= 0) {
		return "";
	}
	if (end_line < start_line) {
		end_line = start_line;
	}

	fs::path full = fs::path(file_part).is_absolute() ? fs::path(file_part) : (root_path / file_part);
	std::string err;
	auto mf = mapped_file::get_cached(full.string(), err, /*populate=*/false);
	if (!mf) {
		return "";
	}
	auto lines = mf->lines();
	if (static_cast<size_t>(start_line) > lines.size()) {
		return "";
	}
	int clamped_end = std::min(end_line, static_cast<int>(lines.size()));
	constexpr int kMaxSrcLines = 15;
	bool truncated = (clamped_end - start_line + 1) > kMaxSrcLines;
	int show_end = truncated ? (start_line + kMaxSrcLines - 1) : clamped_end;

	// Dedent by the longest common leading whitespace prefix across non-empty lines
	// so single-line snippets trim cleanly while multi-line blocks preserve relative indentation.
	std::string common_prefix;
	bool has_non_empty = false;
	for (int ln = start_line; ln <= show_end; ++ln) {
		std::string_view sv = lines[static_cast<size_t>(ln) - 1];
		size_t indent = 0;
		while (indent < sv.size() && (sv[indent] == ' ' || sv[indent] == '\t')) {
			++indent;
		}
		if (indent == sv.size()) {
			continue; // blank/whitespace-only line
		}
		std::string_view ws = sv.substr(0, indent);
		if (!has_non_empty) {
			common_prefix = std::string(ws);
			has_non_empty = true;
		} else {
			size_t match = 0;
			while (match < common_prefix.size() && match < ws.size() && common_prefix[match] == ws[match]) {
				++match;
			}
			common_prefix.resize(match);
		}
	}

	std::string out;
	for (int ln = start_line; ln <= show_end; ++ln) {
		if (ln > start_line) {
			out.push_back('\n');
		}
		std::string_view sv = lines[static_cast<size_t>(ln) - 1];
		size_t indent = 0;
		while (indent < sv.size() && (sv[indent] == ' ' || sv[indent] == '\t')) {
			++indent;
		}
		if (indent == sv.size()) {
			continue;
		}
		if (!common_prefix.empty() && sv.starts_with(common_prefix)) {
			sv.remove_prefix(common_prefix.size());
		}
		out.append(sv);
	}
	if (truncated) {
		out += std::format("\n... ({} more line(s))", clamped_end - show_end);
	}
	return out;
}

nlohmann::json format_yosys_params(const nlohmann::json &raw_params)
{
	if (!raw_params.is_object()) {
		return nlohmann::json::object();
	}
	nlohmann::json out = nlohmann::json::object();
	for (const auto &[k, v] : raw_params.items()) {
		if (!v.is_string()) {
			out[k] = v;
			continue;
		}
		const std::string &s = v.get_ref<const std::string &>();
		bool all_bin = !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c == '0' || c == '1'; });
		if (!all_bin) {
			out[k] = v;
			continue;
		}
		if (k == "LUT") {
			out[k] = s;
			continue;
		}
		if (s.size() <= 64 && (k == "WIDTH" || k.ends_with("_WIDTH") || k.ends_with("_SIGNED") || k == "OFFSET" ||
				       k == "ABITS" || k == "SIZE" || k == "RD_PORTS" || k == "WR_PORTS")) {
			uint64_t val = 0;
			for (char c : s) {
				val = (val << 1) | static_cast<uint64_t>(c == '1' ? 1 : 0);
			}
			out[k] = val;
		} else {
			// Format MSB-first binary string to 0x... hex
			std::string hex;
			size_t pad = (4 - (s.size() % 4)) % 4;
			std::string padded = std::string(pad, '0') + s;
			for (size_t i = 0; i < padded.size(); i += 4) {
				int nib = ((padded[i] == '1') << 3) | ((padded[i + 1] == '1') << 2) |
					  ((padded[i + 2] == '1') << 1) | (padded[i + 3] == '1');
				hex.push_back(static_cast<char>(nib < 10 ? ('0' + nib) : ('a' + nib - 10)));
			}
			out[k] = std::format("0x{}", hex);
		}
	}
	return out;
}

std::string permute_lut_bits(std::string_view orig_lut, const std::vector<size_t> &perm)
{
	const size_t w = perm.size();
	const size_t n = size_t{1} << w;
	if (orig_lut.size() != n) {
		return std::string(orig_lut);
	}
	std::string out(n, '0');
	for (size_t new_k = 0; new_k < n; ++new_k) {
		size_t old_k = 0;
		for (size_t j = 0; j < w; ++j) {
			if ((new_k >> j) & 1U) {
				old_k |= (size_t{1} << perm[j]);
			}
		}
		out[n - 1 - new_k] = orig_lut[n - 1 - old_k];
	}
	return out;
}

std::string clean_paramod_type(std::string_view raw_type)
{
	if (!raw_type.starts_with("$paramod\\")) {
		return std::string(raw_type);
	}
	raw_type.remove_prefix(9);
	size_t sep = raw_type.find('\\');
	if (sep != std::string_view::npos) {
		return std::string(raw_type.substr(0, sep));
	}
	return std::string(raw_type);
}

} // namespace

bool gridcad_scene_exporter::export_module_to_file(const fs::path &project_root,
						   std::string_view target_module,
						   const fs::path &out_json_path,
						   std::string &out_error,
						   bool bundle_submodules)
{
	std::error_code ec;
	fs::path root_path = fs::weakly_canonical(project_root, ec);
	if (ec) {
		root_path = project_root;
	}

	fpga_target_config cfg;
	std::string cfg_err;
	fpga_project_settings::load_from_project(root_path.string(), cfg, cfg_err);

	std::string sv_rel = cfg.sv_path.empty() ? fpga_project_settings::detect_sv_path(root_path.string()) : cfg.sv_path;
	fs::path scan_root = sv_rel.empty() ? root_path : (root_path / sv_rel);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
	}

	std::vector<std::string> rtl_files = verilog_module_scanner::find_synthesizable_files(scan_root);
	if (rtl_files.empty()) {
		out_error = std::format("No RTL source files found under '{}'", scan_root.string());
		return false;
	}

	std::string top_mod = target_module.empty()
		? cfg.top_module
		: verilog_module_scanner::resolve_module_name(target_module, "", rtl_files);
	if (top_mod.empty()) {
		auto mods = verilog_module_scanner::discover_modules_in_files(rtl_files);
		if (mods.empty()) {
			out_error = "No Verilog/SystemVerilog modules discovered";
			return false;
		}
		top_mod = mods.front().module_name;
	}

	auto discovered_mods = verilog_module_scanner::discover_modules_in_files(rtl_files);
	std::map<std::string, std::string> mod_to_rel_file;
	for (const auto &dm : discovered_mods) {
		fs::path rel = fs::relative(dm.source_file, root_path, ec);
		mod_to_rel_file[dm.module_name] = (!ec && !rel.empty()) ? rel.generic_string() : dm.source_file;
	}

	// Stage temporary techmap, liberty, and output json inside project tmp dir
	fs::path work_dir = fs::path(fs_utils::get_project_tmp_dir()) / "gridcad_export";
	fs::create_directories(work_dir, ec);
	fs::path lib_path = work_dir / "gridcad.lib";
	fs::path map_path = work_dir / "gridcad_blocks.v";
	fs::path yosys_json_path = work_dir / "yosys.json";

	{
		std::ofstream lf(lib_path, std::ios::binary | std::ios::trunc);
		lf << kGridcadLiberty;
		std::ofstream mf(map_path, std::ios::binary | std::ios::trunc);
		mf << kGridcadTechmap;
	}

	// Collect hierarchy files for top_mod
	std::vector<std::string> hier_mods = verilog_module_scanner::discover_submodule_hierarchy(top_mod, rtl_files);
	std::set<std::string> needed_mods(hier_mods.begin(), hier_mods.end());
	needed_mods.insert(top_mod);
	std::vector<std::string> read_files;
	std::set<std::string> seen_files;
	for (const auto &dm : discovered_mods) {
		if (needed_mods.contains(dm.module_name) && seen_files.insert(dm.source_file).second) {
			read_files.push_back(dm.source_file);
		}
	}
	if (read_files.empty()) {
		read_files = rtl_files;
	}

	std::string read_cmds;
	for (const auto &rf : read_files) {
		read_cmds += std::format("read_verilog -sv \"{}\"\n", rf);
	}

	fs::path ys_path = work_dir / "export.ys";
	{
		std::ofstream yf(ys_path, std::ios::binary | std::ios::trunc);
		yf << read_cmds
		   << std::format("hierarchy -check -top {}\n", top_mod)
		   << "proc\nopt\nmemory_collect\n"
		   << std::format("techmap -map \"{}\"\n", map_path.string())
		   << std::format("read_verilog -lib \"{}\"\n", map_path.string())
		   << "techmap\n"
		   << std::format("dfflibmap -liberty \"{}\"\n", lib_path.string())
		   << "abc -lut 8\n"
		   << "opt_clean\n"
		   << std::format("write_json \"{}\"\n", yosys_json_path.string());
	}

	std::string yosys_cmd = std::format("cd {} && yosys -q -s {}",
					    fs_utils::escape_shell_arg(root_path.string()),
					    fs_utils::escape_shell_arg(ys_path.string()));
	int yrc = 0;
	std::string yosys_out = fs_utils::execute_command_sync(yosys_cmd, yrc);
	if (yrc != 0 || !fs::exists(yosys_json_path, ec)) {
		out_error = std::format("Yosys gridcad export failed (exit {}):\n{}", yrc, yosys_out);
		return false;
	}

	mapped_file y_mf;
	if (!y_mf.open(yosys_json_path.string(), out_error, /*populate=*/true)) {
		return false;
	}

	nlohmann::json yj;
	try {
		yj = nlohmann::json::parse(y_mf.string_view());
	} catch (const std::exception &ex) {
		out_error = std::format("Failed to parse Yosys JSON: {}", ex.what());
		return false;
	}

	// Build shared label_tables from vcd_labels.json (virtual enums, highest priority)
	// and auto-discovered SystemVerilog enums.
	const std::string vcd_labels_path = (root_path / "vcd_labels.json").string();
	auto vcd_labels = vcd_label_manager::get_instance().get_labels_for_file(vcd_labels_path);
	auto enum_labels = verilog_module_scanner::discover_enum_signal_labels(scan_root);
	nlohmann::json label_tables = nlohmann::json::object();
	std::vector<std::pair<std::string, std::string>> ordered_pattern_to_table;
	std::map<std::string, std::string> map_hash_to_table_id;
	size_t label_idx = 0;
	size_t enum_idx = 0;

	auto register_label_map = [&](const std::map<std::string, std::map<std::string, std::string>> &src_map,
				      std::string_view prefix, size_t &counter) {
		for (const auto &[sig_pat, val_map] : src_map) {
			if (val_map.empty()) continue;
			nlohmann::json tbl = val_map;
			std::string dumped = tbl.dump();
			auto it = map_hash_to_table_id.find(dumped);
			if (it == map_hash_to_table_id.end()) {
				std::string tid = std::format("{}_{}", prefix, counter++);
				label_tables[tid] = std::move(tbl);
				map_hash_to_table_id[dumped] = tid;
				ordered_pattern_to_table.emplace_back(sig_pat, tid);
			} else {
				ordered_pattern_to_table.emplace_back(sig_pat, it->second);
			}
		}
	};

	register_label_map(vcd_labels, "label", label_idx);
	register_label_map(enum_labels, "enum", enum_idx);

	auto find_label_table_for_wire = [&](std::string_view mod_name, std::string_view wire_name) -> std::string {
		std::string full_cand = std::format("{}.{}", mod_name, wire_name);
		for (const auto &[pat, tid] : ordered_pattern_to_table) {
			if (vcd_label_manager::signal_pattern_matches(pat, full_cand) ||
			    vcd_label_manager::signal_pattern_matches(pat, wire_name)) {
				return tid;
			}
		}
		return "";
	};

	const auto &ymods = yj["modules"];
	std::set<std::string> all_mod_names;
	for (const auto &[mname, mobj] : ymods.items()) {
		if (mobj.contains("attributes") && mobj["attributes"].value("blackbox", "") == "00000000000000000000000000000001") {
			continue;
		}
		all_mod_names.insert(mname);
	}

	nlohmann::json scene;
	scene["project_dir"] = root_path.generic_string();
	scene["project_name"] = root_path.filename().generic_string();
	scene["top_module"] = top_mod;
	scene["label_tables"] = std::move(label_tables);
	scene["modules"] = nlohmann::json::object();

	for (const auto &mname : all_mod_names) {
		if (!bundle_submodules && mname != top_mod) {
			continue;
		}
		const auto &ymod = ymods[mname];

		// Human port declaration order via verilog_module_scanner
		auto rtl_ports = verilog_module_scanner::extract_module_ports(mname, rtl_files);
		std::map<std::string, int> port_order_map;
		for (size_t i = 0; i < rtl_ports.size(); ++i) {
			port_order_map[rtl_ports[i].name] = static_cast<int>(i);
		}

		struct net_meta {
			std::string name;
			bool is_glue{true};
			std::string src;
			std::string src_verilog;
		};
		std::map<std::string, net_meta> net_by_bits;
		if (ymod.contains("netnames") && ymod["netnames"].is_object()) {
			for (const auto &[nname, ndata] : ymod["netnames"].items()) {
				std::string bkey = bits_key(ndata["bits"]);
				bool is_glue = ndata.value("hide_name", 0) != 0;
				auto it = net_by_bits.find(bkey);
				if (it == net_by_bits.end() || (it->second.is_glue && !is_glue)) {
					std::string clean_src;
					if (ndata.contains("attributes") && ndata["attributes"].contains("src") &&
					    ndata["attributes"]["src"].is_string()) {
						clean_src = normalize_rel_src(root_path, yosys_netlist_reader::clean_src_location(
							ndata["attributes"]["src"].get<std::string>()));
					}
					net_by_bits[bkey] = {nname, is_glue, clean_src,
							     read_range_src(root_path, clean_src)};
				}
			}
		}

		size_t glue_counter = 0;
		size_t const_counter = 0;
		std::map<std::string, nlohmann::json> wires_map;
		std::map<std::string, nlohmann::json> wire_yosys_bits;
		std::vector<std::string> wire_order;
		nlohmann::json constants_arr = nlohmann::json::array();

		auto get_wire = [&](const nlohmann::json &bits_arr) -> std::string {
			if (is_all_const_bits(bits_arr)) {
				std::string val_hex = format_const_hex(bits_arr);
				std::string cid = std::format("const_{}", const_counter++);
				std::string wname = std::format("wire_const_{}", const_counter);
				constants_arr.push_back({
					{"id", cid},
					{"value", val_hex},
					{"width", bits_arr.size()},
					{"wire", wname}
				});
				wires_map[wname] = {
					{"name", wname},
					{"is_glue", true},
					{"is_const", true},
					{"width", bits_arr.size()},
					{"driver", {{"kind", "constant"}, {"id", cid}, {"pin", "out"}}},
					{"sinks", nlohmann::json::array()}
				};
				wire_order.push_back(wname);
				return wname;
			}

			std::string bkey = bits_key(bits_arr);
			auto it = net_by_bits.find(bkey);
			std::string wname;
			bool is_glue = true;
			std::string src;
			std::string src_v;
			if (it != net_by_bits.end()) {
				is_glue = it->second.is_glue;
				src = it->second.src;
				src_v = it->second.src_verilog;
				if (!is_glue) {
					wname = it->second.name;
				} else if (it->second.name.starts_with("wire_")) {
					wname = it->second.name;
				}
			}
			if (wname.empty()) {
				wname = std::format("wire_{}", ++glue_counter);
				net_by_bits[bkey] = {wname, true, src, src_v};
			}
			if (!wires_map.contains(wname)) {
				nlohmann::json wobj = {
					{"name", wname},
					{"is_glue", is_glue},
					{"width", bits_arr.size()},
					{"driver", nullptr},
					{"sinks", nlohmann::json::array()}
				};
				if (!is_glue) {
					wobj["vcd_signal"] = wname;
					std::string ltbl = find_label_table_for_wire(mname, wname);
					if (!ltbl.empty()) {
						wobj["label_table"] = ltbl;
					}
					std::string full_cand = std::format("{}.{}", mname, wname);
					auto [q_sig, q_active_low] = vcd_label_manager::get_instance().find_qualifier(vcd_labels_path, full_cand);
					if (q_sig.empty()) {
						std::tie(q_sig, q_active_low) = vcd_label_manager::get_instance().find_qualifier(vcd_labels_path, wname);
					}
					if (!q_sig.empty()) {
						// Normalize to module-local signal name (strip leading '*' or hierarchical scope prefix)
						while (!q_sig.empty() && q_sig.front() == '*') {
							q_sig.erase(0, 1);
						}
						if (size_t dot = q_sig.rfind('.'); dot != std::string::npos) {
							q_sig.erase(0, dot + 1);
						}
						if (!q_sig.empty() && ymod.contains("netnames") && ymod["netnames"].contains(q_sig)) {
							wobj["valid_signal"] = q_sig;
							if (q_active_low) {
								wobj["valid_active_low"] = true;
							}
						}
					}
				}
				if (!src.empty()) wobj["src"] = src;
				if (!src_v.empty()) wobj["src_verilog"] = src_v;
				wires_map[wname] = std::move(wobj);
				wire_yosys_bits[wname] = bits_arr;
				wire_order.push_back(wname);
			}
			return wname;
		};

		nlohmann::json ports_arr = nlohmann::json::array();
		if (ymod.contains("ports") && ymod["ports"].is_object()) {
			for (const auto &[pname, pdata] : ymod["ports"].items()) {
				std::string wname = get_wire(pdata["bits"]);
				std::string pdir = pdata.value("direction", "input");
				if (pdir == "input") {
					wires_map[wname]["driver"] = {{"kind", "port"}, {"id", pname}, {"pin", pname}};
				} else {
					wires_map[wname]["sinks"].push_back({{"kind", "port"}, {"id", pname}, {"pin", pname}});
				}
				nlohmann::json pobj = {
					{"name", pname},
					{"direction", pdir},
					{"width", pdata["bits"].size()},
					{"wire", wname}
				};
				auto oit = port_order_map.find(pname);
				if (oit != port_order_map.end()) {
					pobj["order"] = oit->second;
				}
				if (wires_map[wname].contains("src")) {
					pobj["src"] = wires_map[wname]["src"];
					if (wires_map[wname].contains("src_verilog")) {
						pobj["src_verilog"] = wires_map[wname]["src_verilog"];
					}
				}
				ports_arr.push_back(std::move(pobj));
			}
		}

		nlohmann::json comps_arr = nlohmann::json::array();
		if (ymod.contains("cells") && ymod["cells"].is_object()) {
			size_t cidx = 0;
			for (const auto &[cname, cdata] : ymod["cells"].items()) {
				std::string raw_ctype = cdata.value("type", "");
				bool is_sub = all_mod_names.contains(raw_ctype);
				std::string ctype = is_sub ? clean_paramod_type(raw_ctype)
							   : (raw_ctype == "$mem_v2" ? "gridcad_mem" : raw_ctype);

				nlohmann::json params = format_yosys_params(cdata.contains("parameters") ? cdata["parameters"] : nlohmann::json::object());
				nlohmann::json conns = cdata.contains("connections") ? cdata["connections"] : nlohmann::json::object();

				if (raw_ctype == "$lut" && params.contains("LUT") && params["LUT"].is_string() &&
				    conns.contains("A") && conns["A"].is_array() && conns.contains("Y")) {
					const std::string lut_bits = params["LUT"].get<std::string>();
					size_t w = conns["A"].size();
					if (w == 1 && (lut_bits == "01" || lut_bits == "10")) {
						ctype = (lut_bits == "01") ? "INV" : "BUF";
						params = nlohmann::json::object();
					} else if (w == 2 && (lut_bits == "1000" || lut_bits == "1110" || lut_bits == "0110" ||
							      lut_bits == "0111" || lut_bits == "0001")) {
						if (lut_bits == "1000") ctype = "AND2";
						else if (lut_bits == "1110") ctype = "OR2";
						else if (lut_bits == "0110") ctype = "XOR2";
						else if (lut_bits == "0111") ctype = "NAND2";
						else ctype = "NOR2";
						nlohmann::json split_conns = nlohmann::json::object();
						split_conns["A"] = nlohmann::json::array({conns["A"][0]});
						split_conns["B"] = nlohmann::json::array({conns["A"][1]});
						split_conns["Y"] = conns["Y"];
						conns = std::move(split_conns);
						params = nlohmann::json::object();
					} else {
						ctype = "gridcad_lut";
						nlohmann::json split_conns = nlohmann::json::object();
						for (size_t i = 0; i < w; ++i) {
							split_conns[std::format("A{}", i)] = nlohmann::json::array({conns["A"][i]});
						}
						split_conns["Y"] = conns["Y"];
						conns = std::move(split_conns);
					}
				} else if (raw_ctype == "gridcad_pmux" && params.contains("WIDTH") && params.contains("S_WIDTH") &&
					   conns.contains("a") && conns.contains("b") && conns["b"].is_array() &&
					   conns.contains("s") && conns["s"].is_array() && conns.contains("y")) {
					const size_t w = params["WIDTH"].get<size_t>();
					const size_t sw = params["S_WIDTH"].get<size_t>();
					if (w > 0 && sw >= 2 && conns["b"].size() == w * sw && conns["s"].size() == sw) {
						nlohmann::json split_conns = nlohmann::json::object();
						split_conns["a"] = conns["a"];
						for (size_t i = 0; i < sw; ++i) {
							nlohmann::json b_slice = nlohmann::json::array();
							for (size_t k = 0; k < w; ++k) {
								b_slice.push_back(conns["b"][i * w + k]);
							}
							split_conns[std::format("b{}", i)] = std::move(b_slice);
							split_conns[std::format("s{}", i)] = nlohmann::json::array({conns["s"][i]});
						}
						split_conns["y"] = conns["y"];
						conns = std::move(split_conns);
					}
				}

				std::string cid = std::format("{}_{}", ctype, cidx++);
				nlohmann::json pins_obj = nlohmann::json::object();
				const auto &pdirs = cdata.contains("port_directions") ? cdata["port_directions"] : nlohmann::json::object();
				if (conns.is_object()) {
					for (const auto &[pin_name, bits] : conns.items()) {
						std::string pdir = pdirs.value(pin_name, "");
						if (pdir.empty()) {
							pdir = (pin_name == "Y" || pin_name == "Q" || pin_name == "y" || pin_name == "q")
								? "output"
								: "input";
						}
						std::string wname = get_wire(bits);
						pins_obj[pin_name] = {
							{"direction", pdir},
							{"width", bits.size()},
							{"wire", wname}
						};
						if (pdir == "output") {
							wires_map[wname]["driver"] = {{"kind", "component"}, {"id", cid}, {"pin", pin_name}};
						} else {
							wires_map[wname]["sinks"].push_back({{"kind", "component"}, {"id", cid}, {"pin", pin_name}});
						}
					}
				}

				nlohmann::json cobj = {
					{"id", cid},
					{"instance_name", cname},
					{"type", ctype},
					{"is_submodule", is_sub},
					{"parameters", std::move(params)},
					{"pins", std::move(pins_obj)}
				};
				if (cdata.contains("attributes") && cdata["attributes"].contains("src") &&
				    cdata["attributes"]["src"].is_string()) {
					std::string csrc = normalize_rel_src(root_path, yosys_netlist_reader::clean_src_location(
						cdata["attributes"]["src"].get<std::string>()));
					if (!csrc.empty()) {
						cobj["src"] = csrc;
						std::string sv_line = read_range_src(root_path, csrc);
						if (!sv_line.empty()) {
							cobj["src_verilog"] = sv_line;
						}
					}
				}
				comps_arr.push_back(std::move(cobj));
			}
		}

		// Consolidate gridcad_lut cells sharing the exact same or a subset of input wires into multi-output LUTs
		{
			struct lut_cand {
				size_t orig_idx{0};
				size_t w{0};
				std::vector<std::string> canon_key;
				std::string permuted_lut;
			};
			std::vector<lut_cand> cands;
			std::vector<bool> is_cand(comps_arr.size(), false);

			for (size_t idx = 0; idx < comps_arr.size(); ++idx) {
				const auto &cobj = comps_arr[idx];
				if (cobj["type"] != "gridcad_lut" || !cobj["parameters"].contains("WIDTH") ||
				    !cobj["parameters"].contains("LUT") || !cobj["parameters"]["LUT"].is_string()) {
					continue;
				}
				size_t w = cobj["parameters"]["WIDTH"].get<size_t>();
				if (w == 0 || w > 8 || !cobj["pins"].contains("Y")) {
					continue;
				}
				std::vector<std::pair<std::string, size_t>> in_wires;
				in_wires.reserve(w);
				bool all_present = true;
				for (size_t i = 0; i < w; ++i) {
					std::string pname = std::format("A{}", i);
					if (!cobj["pins"].contains(pname)) {
						all_present = false;
						break;
					}
					in_wires.emplace_back(cobj["pins"][pname]["wire"].get<std::string>(), i);
				}
				if (!all_present) {
					continue;
				}
				std::sort(in_wires.begin(), in_wires.end());
				std::vector<std::string> canon_key;
				std::vector<size_t> perm;
				canon_key.reserve(w);
				perm.reserve(w);
				for (const auto &[wn, orig_i] : in_wires) {
					canon_key.push_back(wn);
					perm.push_back(orig_i);
				}
				std::string permuted_lut = permute_lut_bits(cobj["parameters"]["LUT"].get<std::string>(), perm);
				is_cand[idx] = true;
				cands.push_back({idx, w, std::move(canon_key), std::move(permuted_lut)});
			}

			// Process largest-input LUTs first so smaller subset LUTs merge into them.
			std::stable_sort(cands.begin(), cands.end(), [](const lut_cand &a, const lut_cand &b) {
				return a.w > b.w;
			});

			nlohmann::json merged_comps = nlohmann::json::array();
			for (size_t idx = 0; idx < comps_arr.size(); ++idx) {
				if (!is_cand[idx]) {
					merged_comps.push_back(std::move(comps_arr[idx]));
				}
			}

			std::map<std::vector<std::string>, size_t> exact_leaders;
			std::vector<std::pair<std::vector<std::string>, size_t>> all_leaders;
			std::set<std::string> dropped_comp_ids;

			auto expand_subset_lut = [](std::string_view sub_lut, const std::vector<std::string> &sub_key,
						    const std::vector<std::string> &sup_key) -> std::string {
				const size_t w_sub = sub_key.size();
				const size_t w_sup = sup_key.size();
				const size_t n_sub = size_t{1} << w_sub;
				const size_t n_sup = size_t{1} << w_sup;
				if (sub_lut.size() != n_sub) {
					return std::string(sub_lut);
				}
				std::vector<size_t> pos(w_sub, 0);
				for (size_t j = 0; j < w_sub; ++j) {
					auto it = std::lower_bound(sup_key.begin(), sup_key.end(), sub_key[j]);
					pos[j] = static_cast<size_t>(it - sup_key.begin());
				}
				std::string out(n_sup, '0');
				for (size_t k = 0; k < n_sup; ++k) {
					size_t sub_k = 0;
					for (size_t j = 0; j < w_sub; ++j) {
						if ((k >> pos[j]) & 1U) {
							sub_k |= (size_t{1} << j);
						}
					}
					out[n_sup - 1 - k] = sub_lut[n_sub - 1 - sub_k];
				}
				return out;
			};

			for (auto &cand : cands) {
				auto &cobj = comps_arr[cand.orig_idx];
				const auto &canon_key = cand.canon_key;

				size_t target_leader_idx = SIZE_MAX;
				const std::vector<std::string> *leader_key_ptr = nullptr;
				auto git = exact_leaders.find(canon_key);
				if (git != exact_leaders.end()) {
					target_leader_idx = git->second;
					leader_key_ptr = &git->first;
				} else {
					size_t best_w = SIZE_MAX;
					for (const auto &[lkey, lidx] : all_leaders) {
						if (lkey.size() > canon_key.size() && lkey.size() < best_w &&
						    std::includes(lkey.begin(), lkey.end(), canon_key.begin(), canon_key.end())) {
							best_w = lkey.size();
							target_leader_idx = lidx;
							leader_key_ptr = &lkey;
						}
					}
				}

				if (target_leader_idx == SIZE_MAX) {
					// Canonicalize leader's input pins to A0..A{w-1} matching sorted canon_key
					std::string out_wire = cobj["pins"]["Y"]["wire"].get<std::string>();
					nlohmann::json new_pins = nlohmann::json::object();
					for (size_t i = 0; i < cand.w; ++i) {
						std::string pname = std::format("A{}", i);
						new_pins[pname] = {{"direction", "input"}, {"width", 1}, {"wire", canon_key[i]}};
						for (auto &sk : wires_map[canon_key[i]]["sinks"]) {
							if (sk["id"] == cobj["id"]) {
								sk["pin"] = pname;
							}
						}
					}
					new_pins["Y"] = {{"direction", "output"}, {"width", 1}, {"wire", out_wire}};
					cobj["pins"] = std::move(new_pins);
					cobj["parameters"]["LUT"] = cand.permuted_lut;
					size_t lidx = merged_comps.size();
					exact_leaders[canon_key] = lidx;
					all_leaders.emplace_back(canon_key, lidx);
					merged_comps.push_back(std::move(cobj));
				} else {
					auto &leader = merged_comps[target_leader_idx];
					std::string leader_id = leader["id"].get<std::string>();
					dropped_comp_ids.insert(cobj["id"].get<std::string>());

					std::string merged_lut = (leader_key_ptr->size() == cand.w)
						? cand.permuted_lut
						: expand_subset_lut(cand.permuted_lut, canon_key, *leader_key_ptr);

					// Convert leader from single "Y" to "Y0", "Y1", ... on first merge
					if (leader["pins"].contains("Y")) {
						leader["pins"]["Y0"] = leader["pins"]["Y"];
						leader["pins"].erase("Y");
						std::string y0_wire = leader["pins"]["Y0"]["wire"].get<std::string>();
						if (wires_map[y0_wire]["driver"].is_object() &&
						    wires_map[y0_wire]["driver"]["id"] == leader_id) {
							wires_map[y0_wire]["driver"]["pin"] = "Y0";
						}
						leader["parameters"]["LUT"] = nlohmann::json::array({leader["parameters"]["LUT"]});
					}

					size_t out_idx = leader["parameters"]["LUT"].size();
					std::string out_pin = std::format("Y{}", out_idx);
					std::string out_wire = cobj["pins"]["Y"]["wire"].get<std::string>();
					leader["pins"][out_pin] = {{"direction", "output"}, {"width", 1}, {"wire", out_wire}};
					leader["parameters"]["LUT"].push_back(std::move(merged_lut));
					leader["parameters"]["OUTPUTS"] = out_idx + 1;
					wires_map[out_wire]["driver"] = {{"kind", "component"}, {"id", leader_id}, {"pin", out_pin}};
				}
			}

			if (!dropped_comp_ids.empty()) {
				for (auto &[wn, wobj] : wires_map) {
					nlohmann::json kept_sinks = nlohmann::json::array();
					for (const auto &sk : wobj["sinks"]) {
						if (!dropped_comp_ids.contains(sk["id"].get<std::string>())) {
							kept_sinks.push_back(sk);
						}
					}
					wobj["sinks"] = std::move(kept_sinks);
				}
			}
			comps_arr = std::move(merged_comps);
		}

		// Synthesize gridcad_slice and gridcad_concat for any undriven wire that
		// taps a sub-range of a driven bus or concatenates multiple signals/constants.
		{
			struct bit_driver_info {
				std::string wire_name;
				size_t offset{0};
				size_t width{0};
			};
			std::map<int64_t, bit_driver_info> bit_to_driver;
			for (const auto &wn : wire_order) {
				if (wires_map[wn]["driver"].is_null()) {
					continue;
				}
				auto bit_it = wire_yosys_bits.find(wn);
				if (bit_it == wire_yosys_bits.end() || !bit_it->second.is_array()) {
					continue;
				}
				const auto &bits = bit_it->second;
				const size_t w_width = bits.size();
				for (size_t i = 0; i < w_width; ++i) {
					if (!bits[i].is_number_integer()) {
						continue;
					}
					int64_t bid = bits[i].get<int64_t>();
					auto existing = bit_to_driver.find(bid);
					if (existing == bit_to_driver.end() || w_width > existing->second.width) {
						bit_to_driver[bid] = {wn, i, w_width};
					}
				}
			}

			// Pack contiguous multi-bit bus inputs on gridcad_lut into vector pins
			// ("A" for a single vector input, or "A0".."A{k-1}" for mixed-width inputs)
			// so full buses wire directly without 8:1 splitters.
			for (auto &cobj : comps_arr) {
				if (cobj["type"] != "gridcad_lut" || !cobj["parameters"].contains("WIDTH") ||
				    !cobj["parameters"].contains("LUT")) {
					continue;
				}
				size_t w = cobj["parameters"]["WIDTH"].get<size_t>();
				if (w < 2) {
					continue;
				}
				struct lut_bit_ref {
					size_t orig_idx{0};
					std::string orig_wire;
					bool from_bus{false};
					std::string src_wire;
					size_t src_width{1};
					size_t offset{0};
				};
				std::vector<lut_bit_ref> bits_info;
				bits_info.reserve(w);
				bool ok = true;
				for (size_t i = 0; i < w; ++i) {
					std::string pname = std::format("A{}", i);
					if (!cobj["pins"].contains(pname)) {
						ok = false;
						break;
					}
					std::string wn = cobj["pins"][pname]["wire"].get<std::string>();
					lut_bit_ref ref;
					ref.orig_idx = i;
					ref.orig_wire = wn;
					auto bit_it = wire_yosys_bits.find(wn);
					if (bit_it != wire_yosys_bits.end() && bit_it->second.is_array() &&
					    bit_it->second.size() == 1 && bit_it->second[0].is_number_integer()) {
						auto dit = bit_to_driver.find(bit_it->second[0].get<int64_t>());
						if (dit != bit_to_driver.end() && dit->second.width >= 2) {
							ref.from_bus = true;
							ref.src_wire = dit->second.wire_name;
							ref.src_width = dit->second.width;
							ref.offset = dit->second.offset;
						}
					}
					bits_info.push_back(std::move(ref));
				}
				if (!ok) {
					continue;
				}

				// Sort bus-sourced bits by (src_wire, offset) so contiguous slices group together,
				// keeping standalone bits in their original relative order after bus bits.
				std::stable_sort(bits_info.begin(), bits_info.end(), [](const lut_bit_ref &a, const lut_bit_ref &b) {
					if (a.from_bus != b.from_bus) {
						return a.from_bus > b.from_bus;
					}
					if (a.from_bus) {
						if (a.src_wire != b.src_wire) {
							return a.src_wire < b.src_wire;
						}
						return a.offset < b.offset;
					}
					return a.orig_idx < b.orig_idx;
				});

				struct packed_group {
					bool is_bus_slice{false};
					std::string src_wire;
					size_t src_width{1};
					size_t start_offset{0};
					std::vector<lut_bit_ref> items;
				};
				std::vector<packed_group> groups;
				for (const auto &ref : bits_info) {
					if (ref.from_bus && !groups.empty() && groups.back().is_bus_slice &&
					    groups.back().src_wire == ref.src_wire &&
					    groups.back().start_offset + groups.back().items.size() == ref.offset) {
						groups.back().items.push_back(ref);
					} else {
						packed_group g;
						g.is_bus_slice = ref.from_bus;
						g.src_wire = ref.src_wire;
						g.src_width = ref.src_width;
						g.start_offset = ref.offset;
						g.items.push_back(ref);
						groups.push_back(std::move(g));
					}
				}

				bool any_packed = std::any_of(groups.begin(), groups.end(), [](const packed_group &g) {
					return g.items.size() >= 2;
				});
				if (!any_packed) {
					continue;
				}

				std::vector<size_t> perm;
				perm.reserve(w);
				for (const auto &g : groups) {
					for (const auto &item : g.items) {
						perm.push_back(item.orig_idx);
					}
				}

				auto &lut_param = cobj["parameters"]["LUT"];
				if (lut_param.is_string()) {
					lut_param = permute_lut_bits(lut_param.get<std::string>(), perm);
				} else if (lut_param.is_array()) {
					for (auto &entry : lut_param) {
						if (entry.is_string()) {
							entry = permute_lut_bits(entry.get<std::string>(), perm);
						}
					}
				}

				const std::string cid = cobj["id"].get<std::string>();
				for (size_t i = 0; i < w; ++i) {
					std::string old_pname = std::format("A{}", i);
					std::string old_wn = cobj["pins"][old_pname]["wire"].get<std::string>();
					cobj["pins"].erase(old_pname);
					nlohmann::json kept_sinks = nlohmann::json::array();
					for (const auto &sk : wires_map[old_wn]["sinks"]) {
						if (!(sk["id"] == cid && sk["pin"] == old_pname)) {
							kept_sinks.push_back(sk);
						}
					}
					wires_map[old_wn]["sinks"] = std::move(kept_sinks);
				}

				for (size_t gi = 0; gi < groups.size(); ++gi) {
					const auto &g = groups[gi];
					std::string pin_name = (groups.size() == 1) ? "A" : std::format("A{}", gi);
					std::string pin_wire;
					if (g.items.size() == 1) {
						pin_wire = g.items[0].orig_wire;
					} else if (g.start_offset == 0 && g.items.size() == g.src_width) {
						pin_wire = g.src_wire;
					} else {
						const auto &src_bits = wire_yosys_bits[g.src_wire];
						nlohmann::json seg_bits = nlohmann::json::array();
						for (size_t k = 0; k < g.items.size(); ++k) {
							seg_bits.push_back(src_bits[g.start_offset + k]);
						}
						pin_wire = get_wire(seg_bits);
					}
					cobj["pins"][pin_name] = {
						{"direction", "input"},
						{"width", g.items.size()},
						{"wire", pin_wire}
					};
					wires_map[pin_wire]["sinks"].push_back({
						{"kind", "component"},
						{"id", cid},
						{"pin", pin_name}
					});
				}
			}

			size_t slice_counter = 0;
			size_t concat_counter = 0;
			std::map<std::tuple<std::string, size_t, size_t>, std::string> slice_cache;

			auto ensure_slice = [&](const std::string &src_wire, size_t in_width, size_t out_width,
						size_t offset, const nlohmann::json &seg_bits,
						const std::string &preferred_out_wire) -> std::string {
				if (offset == 0 && out_width == in_width) {
					return src_wire;
				}
				auto key = std::make_tuple(src_wire, out_width, offset);
				auto sit = slice_cache.find(key);
				if (sit != slice_cache.end() && preferred_out_wire.empty()) {
					return sit->second;
				}
				std::string out_w = preferred_out_wire.empty() ? get_wire(seg_bits) : preferred_out_wire;
				if (wires_map[out_w]["driver"].is_null()) {
					std::string sid = std::format("gridcad_slice_{}", slice_counter++);
					comps_arr.push_back({
						{"id", sid},
						{"type", "gridcad_slice"},
						{"parameters", {{"IN_WIDTH", in_width}, {"OUT_WIDTH", out_width}, {"OFFSET", offset}}},
						{"pins", {
							{"in", {{"direction", "input"}, {"width", in_width}, {"wire", src_wire}}},
							{"out", {{"direction", "output"}, {"width", out_width}, {"wire", out_w}}}
						}}
					});
					wires_map[out_w]["driver"] = {{"kind", "component"}, {"id", sid}, {"pin", "out"}};
					wires_map[src_wire]["sinks"].push_back({{"kind", "component"}, {"id", sid}, {"pin", "in"}});
				}
				slice_cache.emplace(key, out_w);
				return out_w;
			};

			struct wire_seg {
				enum class kind_t { signal, constant, unknown } kind{kind_t::unknown};
				std::string src_wire;
				size_t src_width{0};
				size_t offset{0};
				nlohmann::json seg_bits = nlohmann::json::array();
			};

			std::vector<std::string> initial_wires = wire_order;
			for (const auto &wn : initial_wires) {
				if (!wires_map[wn]["driver"].is_null() || wires_map[wn]["sinks"].empty()) {
					continue;
				}
				auto bit_it = wire_yosys_bits.find(wn);
				if (bit_it == wire_yosys_bits.end() || !bit_it->second.is_array() || bit_it->second.empty()) {
					continue;
				}
				const auto &bits = bit_it->second;

				std::vector<wire_seg> segs;
				bool has_known = false;
				for (size_t i = 0; i < bits.size(); ++i) {
					const auto &b = bits[i];
					if (b.is_string()) {
						has_known = true;
						if (!segs.empty() && segs.back().kind == wire_seg::kind_t::constant) {
							segs.back().seg_bits.push_back(b);
						} else {
							wire_seg s;
							s.kind = wire_seg::kind_t::constant;
							s.seg_bits.push_back(b);
							segs.push_back(std::move(s));
						}
					} else if (b.is_number_integer()) {
						auto dit = bit_to_driver.find(b.get<int64_t>());
						if (dit != bit_to_driver.end()) {
							has_known = true;
							const auto &dinfo = dit->second;
							if (!segs.empty() && segs.back().kind == wire_seg::kind_t::signal &&
							    segs.back().src_wire == dinfo.wire_name &&
							    segs.back().offset + segs.back().seg_bits.size() == dinfo.offset) {
								segs.back().seg_bits.push_back(b);
							} else {
								wire_seg s;
								s.kind = wire_seg::kind_t::signal;
								s.src_wire = dinfo.wire_name;
								s.src_width = dinfo.width;
								s.offset = dinfo.offset;
								s.seg_bits.push_back(b);
								segs.push_back(std::move(s));
							}
						} else {
							if (!segs.empty() && segs.back().kind == wire_seg::kind_t::unknown) {
								segs.back().seg_bits.push_back(b);
							} else {
								wire_seg s;
								s.kind = wire_seg::kind_t::unknown;
								s.seg_bits.push_back(b);
								segs.push_back(std::move(s));
							}
						}
					}
				}

				if (!has_known || segs.empty()) {
					continue;
				}
				if (segs.size() == 1) {
					if (segs[0].kind == wire_seg::kind_t::signal) {
						ensure_slice(segs[0].src_wire, segs[0].src_width,
							     segs[0].seg_bits.size(), segs[0].offset,
							     segs[0].seg_bits, wn);
					}
					continue;
				}

				nlohmann::json concat_pins = nlohmann::json::object();
				std::vector<std::pair<std::string, std::string>> in_pin_wires;
				in_pin_wires.reserve(segs.size());
				for (size_t i = 0; i < segs.size(); ++i) {
					std::string seg_w;
					if (segs[i].kind == wire_seg::kind_t::signal) {
						seg_w = ensure_slice(segs[i].src_wire, segs[i].src_width,
								     segs[i].seg_bits.size(), segs[i].offset,
								     segs[i].seg_bits, "");
					} else {
						seg_w = get_wire(segs[i].seg_bits);
					}
					std::string pin_name = std::format("in{}", i);
					concat_pins[pin_name] = {
						{"direction", "input"},
						{"width", segs[i].seg_bits.size()},
						{"wire", seg_w}
					};
					in_pin_wires.emplace_back(std::move(pin_name), std::move(seg_w));
				}
				concat_pins["out"] = {
					{"direction", "output"},
					{"width", bits.size()},
					{"wire", wn}
				};
				std::string cid = std::format("gridcad_concat_{}", concat_counter++);
				comps_arr.push_back({
					{"id", cid},
					{"type", "gridcad_concat"},
					{"parameters", {{"OUT_WIDTH", bits.size()}, {"INPUTS", segs.size()}}},
					{"pins", std::move(concat_pins)}
				});
				wires_map[wn]["driver"] = {{"kind", "component"}, {"id", cid}, {"pin", "out"}};
				for (const auto &[pin_name, seg_w] : in_pin_wires) {
					wires_map[seg_w]["sinks"].push_back({{"kind", "component"}, {"id", cid}, {"pin", pin_name}});
				}
			}
		}

		// Consolidate full-bus 1-bit gridcad_slice groups (all 0..IN_WIDTH-1 bits)
		// into a single gridcad_split component.
		{
			struct one_bit_slice_ref {
				size_t comp_idx{0};
				size_t offset{0};
				std::string id;
				std::string out_wire;
			};
			struct split_group {
				size_t in_width{0};
				std::vector<one_bit_slice_ref> slices;
			};
			std::map<std::string, split_group> by_src;
			for (size_t i = 0; i < comps_arr.size(); ++i) {
				const auto &c = comps_arr[i];
				if (c["type"] != "gridcad_slice") {
					continue;
				}
				const auto &p = c["parameters"];
				if (p["OUT_WIDTH"].get<size_t>() != 1) {
					continue;
				}
				size_t in_w = p["IN_WIDTH"].get<size_t>();
				if (in_w <= 1) {
					continue;
				}
				std::string src_w = c["pins"]["in"]["wire"].get<std::string>();
				auto &grp = by_src[src_w];
				grp.in_width = in_w;
				grp.slices.push_back({
					i,
					p["OFFSET"].get<size_t>(),
					c["id"].get<std::string>(),
					c["pins"]["out"]["wire"].get<std::string>()
				});
			}

			std::set<size_t> dropped_indices;
			std::set<std::string> dropped_slice_ids;
			size_t split_counter = 0;
			nlohmann::json new_splits = nlohmann::json::array();

			for (const auto &[src_w, grp] : by_src) {
				if (grp.slices.size() != grp.in_width) {
					continue;
				}
				std::vector<std::string> out_wires(grp.in_width);
				bool all_covered = true;
				for (const auto &s : grp.slices) {
					if (s.offset >= grp.in_width || !out_wires[s.offset].empty()) {
						all_covered = false;
						break;
					}
					out_wires[s.offset] = s.out_wire;
				}
				if (!all_covered) {
					continue;
				}

				std::string sp_id = std::format("gridcad_split_{}", split_counter++);
				nlohmann::json sp_pins = {
					{"in", {{"direction", "input"}, {"width", grp.in_width}, {"wire", src_w}}}
				};
				for (size_t b = 0; b < grp.in_width; ++b) {
					std::string pin_name = std::format("out{}", b);
					sp_pins[pin_name] = {
						{"direction", "output"},
						{"width", 1},
						{"wire", out_wires[b]}
					};
					wires_map[out_wires[b]]["driver"] = {{"kind", "component"}, {"id", sp_id}, {"pin", pin_name}};
				}
				new_splits.push_back({
					{"id", sp_id},
					{"type", "gridcad_split"},
					{"parameters", {{"IN_WIDTH", grp.in_width}, {"OUTPUTS", grp.in_width}}},
					{"pins", std::move(sp_pins)}
				});

				for (const auto &s : grp.slices) {
					dropped_indices.insert(s.comp_idx);
					dropped_slice_ids.insert(s.id);
				}

				nlohmann::json kept_sinks = nlohmann::json::array();
				for (const auto &sk : wires_map[src_w]["sinks"]) {
					if (!dropped_slice_ids.contains(sk["id"].get<std::string>())) {
						kept_sinks.push_back(sk);
					}
				}
				kept_sinks.push_back({{"kind", "component"}, {"id", sp_id}, {"pin", "in"}});
				wires_map[src_w]["sinks"] = std::move(kept_sinks);
			}

			if (!dropped_indices.empty()) {
				nlohmann::json kept_comps = nlohmann::json::array();
				for (size_t i = 0; i < comps_arr.size(); ++i) {
					if (!dropped_indices.contains(i)) {
						kept_comps.push_back(std::move(comps_arr[i]));
					}
				}
				for (auto &sp : new_splits) {
					kept_comps.push_back(std::move(sp));
				}
				comps_arr = std::move(kept_comps);
			}
		}

		nlohmann::json wires_arr = nlohmann::json::array();
		for (const auto &wn : wire_order) {
			const auto &wobj = wires_map[wn];
			if (wobj.value("is_glue", false) && wobj["driver"].is_null() && wobj["sinks"].empty()) {
				continue;
			}
			wires_arr.push_back(std::move(wires_map[wn]));
		}

		nlohmann::json mobj = {
			{"ports", std::move(ports_arr)},
			{"constants", std::move(constants_arr)},
			{"components", std::move(comps_arr)},
			{"wires", std::move(wires_arr)}
		};
		std::string out_mname = clean_paramod_type(mname);
		auto mit = mod_to_rel_file.find(out_mname);
		if (mit != mod_to_rel_file.end()) {
			mobj["source_file"] = mit->second;
		}
		scene["modules"][out_mname] = std::move(mobj);
	}

	if (out_json_path.has_parent_path()) {
		fs::create_directories(out_json_path.parent_path(), ec);
	}
	std::ofstream out_f(out_json_path, std::ios::binary | std::ios::trunc);
	if (!out_f.is_open()) {
		out_error = std::format("Failed to open '{}' for writing", out_json_path.string());
		return false;
	}
	out_f << scene.dump(2) << "\n";
	return true;
}

} // namespace verilog
