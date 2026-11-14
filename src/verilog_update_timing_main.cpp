#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_project_settings_validator.h"
#include "plugins/verilog/verilog_configure_testbench_validator.h"
#include "plugins/verilog/verilog_compile_validator.h"
#include "plugins/verilog/verilog_simulate_validator.h"
#include "plugins/verilog/vcd_file_info_validator.h"
#include "plugins/verilog/vcd_wave_validator.h"
#include "plugins/verilog/vcd_search_validator.h"
#include "plugins/verilog/vcd_compare_validator.h"
#include "plugins/verilog/vcd_signal_to_code_validator.h"
#include "plugins/verilog/vcd_signal_labels_validator.h"
#include "plugins/verilog/vcd_signal_qualifier_validator.h"
#include "agentlib/interactions/action.h"
#include "agentlib/tool_context.h"
#include "fs_utils.h"
#include "markdown_utils.h"

#include <CLI11.hpp>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static std::string_view trim_sv(std::string_view sv)
{
	while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) {
		sv.remove_suffix(1);
	}
	return sv;
}

static std::string pad_right(std::string_view sv, size_t width)
{
	std::string res(sv);
	if (res.size() < width) {
		res.append(width - res.size(), ' ');
	}
	return res;
}

static std::string pad_left(std::string_view sv, size_t width)
{
	std::string res;
	if (sv.size() < width) {
		res.append(width - sv.size(), ' ');
	}
	res.append(sv);
	return res;
}

static bool is_numbered_chain_line(std::string_view line, std::string_view &num_tok, std::string_view &rest)
{
	if (line.size() < 5 || line[0] != ' ' || line[1] != ' ') {
		return false;
	}
	size_t idx = 2;
	while (idx < line.size() && line[idx] >= '0' && line[idx] <= '9') {
		++idx;
	}
	if (idx == 2 || idx + 1 >= line.size() || line[idx] != '.' || line[idx + 1] != ' ') {
		return false;
	}
	num_tok = line.substr(2, idx - 1); // e.g. "1."
	rest = line.substr(idx + 2);
	return true;
}

static std::string align_chain_block(const std::vector<std::string> &block_lines)
{
	struct parsed_row {
		bool is_item = false;
		std::string raw;
		std::string num;
		std::string c1;
		std::string c2;
		std::string c3;
		std::string c4;
		std::string c5;
	};

	std::vector<parsed_row> rows;
	rows.reserve(block_lines.size());

	int block_kind = 0; // 1 = segments, 2 = pnr chain, 3 = logic_depth chain
	for (const auto &line : block_lines) {
		std::string_view num_tok;
		std::string_view rest;
		if (!is_numbered_chain_line(line, num_tok, rest)) {
			rows.push_back({false, line, {}, {}, {}, {}, {}, {}});
			continue;
		}
		if (block_kind == 0) {
			if (rest.starts_with("`[") || rest.starts_with("[")) {
				block_kind = 1;
			} else if (rest.find(" \xe2\x80\x94 ") != std::string_view::npos) {
				block_kind = 2;
			} else if (rest.find(" -> ") != std::string_view::npos) {
				block_kind = 3;
			}
		}

		parsed_row r;
		r.is_item = true;
		r.raw = line;
		r.num = std::string(num_tok);

		if (block_kind == 1) {
			// Segment: `[type]` 0.540 ns — body
			constexpr std::string_view emdash = " \xe2\x80\x94 ";
			size_t dash_pos = rest.find(emdash);
			if (dash_pos != std::string_view::npos) {
				std::string_view left = trim_sv(rest.substr(0, dash_pos));
				std::string_view right = trim_sv(rest.substr(dash_pos + emdash.size()));
				size_t sp = left.find(' ');
				if (sp != std::string_view::npos) {
					r.c1 = std::string(trim_sv(left.substr(0, sp)));   // `[logic]`
					r.c2 = std::string(trim_sv(left.substr(sp + 1)));  // 0.315 ns
				} else {
					r.c1 = std::string(left);
				}
				r.c3 = std::string(right);
			} else {
				r.is_item = false;
			}
		} else if (block_kind == 2) {
			// PnR Chain: `loc` (hint) [| `snippet`] — delay [-> out] [meta]
			constexpr std::string_view emdash = " \xe2\x80\x94 ";
			size_t dash_pos = rest.find(emdash);
			if (dash_pos != std::string_view::npos) {
				std::string_view left = trim_sv(rest.substr(0, dash_pos));
				std::string_view right = trim_sv(rest.substr(dash_pos + emdash.size()));

				std::string_view loc_hint = left;
				std::string_view snippet;
				size_t pipe_pos = left.find(" | ");
				if (pipe_pos != std::string_view::npos) {
					loc_hint = trim_sv(left.substr(0, pipe_pos));
					snippet = trim_sv(left.substr(pipe_pos + 3));
				}

				size_t paren_pos = loc_hint.find(" (");
				if (paren_pos != std::string_view::npos) {
					r.c1 = std::string(trim_sv(loc_hint.substr(0, paren_pos)));
					r.c2 = std::string(trim_sv(loc_hint.substr(paren_pos + 1)));
				} else {
					r.c1 = std::string(loc_hint);
				}
				r.c3 = std::string(snippet);

				size_t close_paren = right.find(')');
				if (close_paren != std::string_view::npos) {
					r.c4 = std::string(trim_sv(right.substr(0, close_paren + 1)));
					r.c5 = std::string(trim_sv(right.substr(close_paren + 1)));
				} else {
					r.c4 = std::string(right);
				}
			} else {
				r.is_item = false;
			}
		} else if (block_kind == 3) {
			// Logic Depth Chain: `loc` (lut_info) -> `out` [| `snippet`]
			std::string_view main_part = rest;
			std::string_view snippet;
			size_t pipe_pos = rest.find(" | ");
			if (pipe_pos != std::string_view::npos) {
				main_part = trim_sv(rest.substr(0, pipe_pos));
				snippet = trim_sv(rest.substr(pipe_pos + 3));
			}
			size_t arrow_pos = main_part.rfind(" -> ");
			if (arrow_pos != std::string_view::npos) {
				std::string_view loc_hint = trim_sv(main_part.substr(0, arrow_pos));
				std::string_view out_net = trim_sv(main_part.substr(arrow_pos + 1)); // "-> `carry`"
				size_t paren_pos = loc_hint.find(" (");
				if (paren_pos != std::string_view::npos) {
					r.c1 = std::string(trim_sv(loc_hint.substr(0, paren_pos)));
					r.c2 = std::string(trim_sv(loc_hint.substr(paren_pos + 1)));
				} else {
					r.c1 = std::string(loc_hint);
				}
				r.c3 = std::string(out_net);
				r.c4 = std::string(snippet);
			} else {
				r.is_item = false;
			}
		} else {
			r.is_item = false;
		}
		rows.push_back(std::move(r));
	}

	if (block_kind == 0) {
		std::string out;
		for (const auto &l : block_lines) {
			out += l + "\n";
		}
		return out;
	}

	size_t w_num = 0, w1 = 0, w2 = 0, w3 = 0, w4 = 0;
	for (const auto &r : rows) {
		if (!r.is_item) continue;
		w_num = std::max(w_num, r.num.size());
		w1 = std::max(w1, r.c1.size());
		w2 = std::max(w2, r.c2.size());
		w3 = std::max(w3, r.c3.size());
		w4 = std::max(w4, r.c4.size());
	}

	std::string out;
	for (const auto &r : rows) {
		if (!r.is_item) {
			out += r.raw + "\n";
			continue;
		}
		if (block_kind == 1) {
			out += std::format("  {} {} {} \xe2\x80\x94 {}\n",
			                   pad_left(r.num, w_num),
			                   pad_right(r.c1, w1),
			                   pad_left(r.c2, w2),
			                   r.c3);
		} else if (block_kind == 2) {
			std::string line = std::format("  {} {}", pad_left(r.num, w_num), pad_right(r.c1, w1));
			if (w2 > 0) {
				line += " " + pad_right(r.c2, w2);
			}
			if (w3 > 0) {
				line += " | " + pad_right(r.c3, w3);
			}
			line += " \xe2\x80\x94 " + (r.c5.empty() ? r.c4 : (pad_right(r.c4, w4) + " " + r.c5));
			out += line + "\n";
		} else if (block_kind == 3) {
			std::string line = std::format("  {} {}", pad_left(r.num, w_num), pad_right(r.c1, w1));
			if (w2 > 0) {
				line += " " + pad_right(r.c2, w2);
			}
			if (!r.c4.empty()) {
				line += " " + pad_right(r.c3, w3) + " | " + r.c4;
			} else {
				line += " " + r.c3;
			}
			out += line + "\n";
		}
	}
	return out;
}

static std::string format_for_terminal(const std::string &md, bool framed_tables)
{
	if (!framed_tables) {
		return markdown_utils::align_all_tables(md, false);
	}
	// Strip inline markdown markers (` and **) inside table rows so terminal column widths match visual width
	std::string prepped;
	prepped.reserve(md.size());
	std::vector<std::string> pending_chain;
	auto flush_chain = [&]() {
		if (!pending_chain.empty()) {
			prepped += align_chain_block(pending_chain);
			pending_chain.clear();
		}
	};

	size_t pos = 0;
	while (pos < md.size()) {
		size_t nl = md.find('\n', pos);
		std::string_view line = (nl == std::string::npos)
			? std::string_view(md).substr(pos)
			: std::string_view(md).substr(pos, nl - pos);
		std::string_view trimmed = line;
		while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
			trimmed.remove_prefix(1);
		}
		if (!trimmed.empty() && trimmed.front() == '|') {
			flush_chain();
			for (size_t i = 0; i < line.size(); ++i) {
				if (line[i] == '`') {
					continue;
				}
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
					++i;
					continue;
				}
				// Replace UTF-8 en-dash/em-dash inside table cells with ASCII '-' so byte-width padding aligns visually
				if (i + 2 < line.size() &&
				    static_cast<unsigned char>(line[i]) == 0xE2 &&
				    static_cast<unsigned char>(line[i + 1]) == 0x80 &&
				    (static_cast<unsigned char>(line[i + 2]) == 0x93 || static_cast<unsigned char>(line[i + 2]) == 0x94)) {
					prepped.push_back('-');
					i += 2;
					continue;
				}
				prepped.push_back(line[i]);
			}
			if (nl != std::string::npos) {
				prepped.push_back('\n');
			}
		} else {
			std::string cleaned_line;
			cleaned_line.reserve(line.size());
			for (size_t i = 0; i < line.size(); ++i) {
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*' &&
				    !(i > 0 && line[i - 1] == '`' && i + 2 < line.size() && line[i + 2] == '`')) {
					++i;
					continue;
				}
				cleaned_line.push_back(line[i]);
			}
			std::string_view num_tok, rest_tok;
			if (is_numbered_chain_line(cleaned_line, num_tok, rest_tok) ||
			    (!pending_chain.empty() && cleaned_line.starts_with("     "))) {
				pending_chain.push_back(std::move(cleaned_line));
			} else {
				flush_chain();
				prepped += cleaned_line;
				if (nl != std::string::npos) {
					prepped.push_back('\n');
				}
			}
		}
		if (nl != std::string::npos) {
			pos = nl + 1;
		} else {
			break;
		}
	}
	flush_chain();
	return markdown_utils::align_all_tables(prepped, true);
}
static std::vector<std::string> split_csv_list(const std::vector<std::string> &raw_items)
{
	std::vector<std::string> out;
	for (const auto &item : raw_items) {
		size_t pos = 0;
		while (pos < item.size()) {
			size_t comma = item.find(',', pos);
			std::string_view tok = (comma == std::string::npos)
				? std::string_view(item).substr(pos)
				: std::string_view(item).substr(pos, comma - pos);
			tok = trim_sv(tok);
			if (!tok.empty()) {
				out.emplace_back(tok);
			}
			if (comma == std::string::npos) {
				break;
			}
			pos = comma + 1;
		}
	}
	return out;
}

static bool parse_kv_pairs(const std::vector<std::string> &raw_items,
                           nlohmann::json &out_obj,
                           std::string &out_err)
{
	out_obj = nlohmann::json::object();
	for (const auto &item : raw_items) {
		for (const auto &tok : split_csv_list({item})) {
			size_t eq = tok.find('=');
			if (eq == std::string::npos || eq == 0) {
				out_err = std::format("Expected 'key=value' pair, got '{}'", tok);
				return false;
			}
			std::string_view k = trim_sv(std::string_view(tok).substr(0, eq));
			size_t val_start = eq + 1;
			if (val_start < tok.size() && tok[val_start] == '=') {
				++val_start; // Support 'sig==val' as well as 'sig=val'
			}
			std::string_view v = trim_sv(std::string_view(tok).substr(val_start));
			if (k.empty() || v.empty()) {
				out_err = std::format("Empty key or value in '{}'", tok);
				return false;
			}
			out_obj[std::string(k)] = std::string(v);
		}
	}
	return true;
}

template <typename ValidatorT>
static int run_validator_tool(const nlohmann::json &args, const std::string &cwd_str, bool framed_tables)
{
	agentlib::tool_context ctx;
	std::error_code ec;
	if (fs::is_directory(cwd_str, ec)) {
		ctx.fs_security.set_working_directory(cwd_str);
	}
	ctx.fs_security.add_allowed_root(cwd_str, agentlib::access_type::write);
	ctx.fs_security.add_allowed_root("/tmp", agentlib::access_type::write);

	ValidatorT validator;
	std::string err;
	if (!validator.validate_args(args, ctx, err)) {
		std::cerr << std::format("Error: {}\n", err);
		return 1;
	}
	auto tool = validator.create_tool(args);
	if (!tool) {
		std::cerr << "Error: Failed to create tool instance.\n";
		return 1;
	}
	std::string out = tool->execute(ctx);
	std::cout << format_for_terminal(out, framed_tables);
	if (!out.empty() && out.back() != '\n') {
		std::cout << "\n";
	}
	auto inter = std::dynamic_pointer_cast<agentlib::interaction_action>(tool->get_interaction());
	if (inter && inter->get_status() == agentlib::interaction_action::status::failure) {
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	verilog::fpga_project_settings::set_cli_mode(true);
	CLI::App app{
		"TurboStar SystemVerilog, FPGA & VCD Toolkit (turbostar-verilog)\n"
		"  Subcommands:\n"
		"    config        Inspect or update turbostar.json FPGA & testbench settings\n"
		"    timing        Logic depth (default), physical PnR (--pnr), or in-place port annotations (-u)\n"
		"    compile       Run Verilator lint, Yosys synthesis & nextpnr triage\n"
		"    sim           Compile and run SystemVerilog testbenches\n"
		"    vcd-info      Inspect VCD timescale, clock, hierarchy scopes, and signal list\n"
		"    vcd-wave      Render clock-aligned VCD waveform tables\n"
		"    vcd-search    Search VCD trace for clock cycles matching signal conditions\n"
		"    vcd-compare   Compare two VCD traces cycle-by-cycle for divergence\n"
		"    vcd-trace     Trace a VCD signal back to its RTL driver and consumer lines\n"
		"    vcd-labels    Register or list symbolic value labels for VCD signals\n"
		"    vcd-qualifier Register or list validity qualifiers for VCD buses"
	};
	app.require_subcommand(0, 1);

	bool mode_pnr = false;
	bool mode_update = false;

	std::string untrusted_path;
	std::string untrusted_module;
	std::string untrusted_target;
	std::string untrusted_netlist;
	std::string untrusted_timing_report;

	size_t top_k = 3;
	int show_code = 1;
	int seeds = 1;
	bool show_segments = false;
	bool all_paths = false;
	bool no_group = false;
	bool no_collapse = false;
	bool no_frames = false;

	bool dry_run = false;
	bool quiet = false;
	bool verbose = false;

	app.add_flag("--pnr", mode_pnr, "Run physical place-and-route timing analysis (nextpnr)");
	app.add_flag("-u,--update", mode_update, "Batch-synthesize all workspace modules and annotate .sv port lines in-place");

	app.add_option("path,-p,--path", untrusted_path, "Target directory, RTL source file, or module name (default: from turbostar.json or '.')");
	app.add_option("-m,--module", untrusted_module, "Target RTL module name or source file (e.g. 'alu8', 'cpu', 'src/alu8.sv')");
	app.add_option("-t,--target", untrusted_target, "Synthesis target ('Generic LUT4', 'Generic LUT6', 'iCE40', 'Generic Gates')");
	app.add_option("-n,--netlist", untrusted_netlist, "Path to pre-synthesized Yosys JSON netlist file (optional)");
	app.add_option("--timing-report", untrusted_timing_report, "Path to pre-generated nextpnr timing JSON report (implies --pnr)");

	app.add_option("-k,--top-k", top_k, "Maximum number of critical paths/bottlenecks to display (default: 3)");
	app.add_option("-c,--show-code", show_code, "RTL source lines per hop: 1=inline snippet (default), >1=multi-line block, 0=off");
	app.add_option("--seeds", seeds, "Number of placement seeds in --pnr mode (default: 1)");
	app.add_flag("--segments", show_segments, "Show full hop-by-hop cell/pin/net segments in --pnr mode");
	app.add_flag("--all-paths", all_paths, "Include async I/O pad paths in --pnr mode (default: Reg -> Reg only)");
	app.add_flag("--no-group", no_group, "Do not group converging paths by sink endpoint");
	app.add_flag("--no-collapse", no_collapse, "Do not collapse multi-hop carry/LUT stages");
	app.add_flag("--no-frames", no_frames, "Use plain pipe Markdown tables instead of UTF-8 box-drawing frames");

	app.add_flag("-d,--dry-run", dry_run, "Dry run in --update mode: analyze all modules without modifying .sv files");
	app.add_flag("-q,--quiet", quiet, "Quiet mode in --update mode: suppress progress output");
	app.add_flag("-v,--verbose", verbose, "Verbose mode: print synthesis failure logs (or enable --segments in --pnr mode)");

	// Subcommand: timing (aliases top-level options via fallthrough)
	auto *sub_timing = app.add_subcommand("timing", "Analyze logic depth, physical PnR (--pnr), or update port annotations (-u)");
	sub_timing->fallthrough();
	sub_timing->add_option("path", untrusted_path, "Target directory, RTL source file, or module name");

	// Subcommand: config (verilog_project_settings + verilog_configure_testbench)
	auto *sub_config = app.add_subcommand("config", "Inspect or update FPGA project and testbench configuration in turbostar.json");
	sub_config->fallthrough();
	std::string cfg_target, cfg_device, cfg_package, cfg_top, cfg_pcf, cfg_sv_path, cfg_yosys_args;
	double cfg_freq = 0.0;
	std::string cfg_tb, cfg_tb_level;
	std::vector<std::string> cfg_tb_modules;
	bool cfg_tb_hier = false;
	bool cfg_tb_no_hier = false;
	bool cfg_tb_remove = false;
	bool cfg_tb_list = false;
	bool cfg_tb_scan = false;
	sub_config->add_option("--target", cfg_target, "Target FPGA architecture (e.g. 'ice40', 'ecp5', 'generic')");
	sub_config->add_option("--device", cfg_device, "FPGA device part (e.g. 'hx8k', 'lp8k')");
	sub_config->add_option("--package", cfg_package, "FPGA package (e.g. 'cb132', 'sg48')");
	sub_config->add_option("--top,--top-module", cfg_top, "Synthesizable top-level module name (e.g. 'main', 'top')");
	sub_config->add_option("--freq,--freq-mhz", cfg_freq, "Target clock frequency in MHz (e.g. 75.0)");
	sub_config->add_option("--pcf,--pcf-file", cfg_pcf, "Path to physical pin constraint file (*.pcf)");
	sub_config->add_option("--sv-path", cfg_sv_path, "Root directory for synthesizable RTL sources (e.g. 'sv', 'src')");
	sub_config->add_option("--yosys-args", cfg_yosys_args, "Optional extra Yosys synthesis arguments (e.g. '-abc9')");
	sub_config->add_option("-b,--tb,--testbench", cfg_tb, "Testbench file to associate in turbostar.json (e.g. 'tb/tb_alu.sv')");
	sub_config->add_option("--modules", cfg_tb_modules, "RTL module(s) verified by --tb (comma-separated or repeatable)");
	sub_config->add_option("--level", cfg_tb_level, "Testbench tier ('quick', 'comprehensive', 'extreme')");
	sub_config->add_flag("--hierarchical", cfg_tb_hier, "Enable hierarchical submodule testing by default for --modules");
	sub_config->add_flag("--no-hierarchical", cfg_tb_no_hier, "Disable hierarchical submodule testing default for --modules");
	sub_config->add_flag("-s,--scan", cfg_tb_scan, "Scan workspace for all testbenches and save auto-discovered RTL bindings to turbostar.json");
	sub_config->add_flag("--remove-tb", cfg_tb_remove, "Remove testbench association specified by --tb");
	sub_config->add_flag("--list-tb", cfg_tb_list, "List configured and auto-discovered testbenches");

	// Subcommand: compile (verilog_compile)
	auto *sub_compile = app.add_subcommand("compile", "Compile and verify RTL with Verilator lint, Yosys synthesis, and nextpnr");
	sub_compile->fallthrough();
	std::string cmp_module, cmp_output;
	bool cmp_lint_only = false;
	bool cmp_synth_only = false;
	sub_compile->add_option("module,-m,--module", cmp_module, "Target RTL module name or source file (default: top_module)");
	sub_compile->add_flag("-l,--lint-only", cmp_lint_only, "Run fast Verilator static lint only");
	sub_compile->add_flag("-s,--synth-only", cmp_synth_only, "Run Verilator lint + Yosys synthesis without nextpnr");
	sub_compile->add_option("-o,--output", cmp_output, "Path to write full raw compilation log");

	// Subcommand: sim (verilog_simulate)
	auto *sub_sim = app.add_subcommand("sim", "Compile and run SystemVerilog/Verilog testbenches");
	sub_sim->alias("simulate");
	sub_sim->fallthrough();
	std::string sim_module, sim_tb, sim_level, sim_top;
	int sim_timeout = 0;
	bool sim_hier = false;
	bool sim_quiet = false;
	bool sim_verbose = false;
	sub_sim->add_option("module,-m,--module", sim_module, "Target RTL module name or source file");
	sub_sim->add_option("-b,--tb,--testbench", sim_tb, "Specific testbench file to run");
	sub_sim->add_flag("-H,--hierarchical", sim_hier, "Also run testbenches for all transitive submodules bottom-up");
	sub_sim->add_option("-l,--level", sim_level, "Test tier filter ('quick', 'comprehensive', 'extreme')");
	sub_sim->add_option("--top", sim_top, "Override top-level testbench module identifier");
	sub_sim->add_option("--timeout", sim_timeout, "Simulation timeout in seconds per testbench");
	sub_sim->add_flag("-q,--quiet,--summary-only", sim_quiet, "Show summary table only for passing testbenches");
	sub_sim->add_flag("-v,--verbose", sim_verbose, "Include full stdout and VCD details for all passing testbenches");

	// Subcommand: vcd-info (vcd_file_info)
	auto *sub_vcd_info = app.add_subcommand("vcd-info", "Inspect VCD file timescale, clock, hierarchy scopes, and signals");
	sub_vcd_info->fallthrough();
	std::string vinfo_path, vinfo_clock, vinfo_scope, vinfo_filter;
	size_t vinfo_max_signals = 0;
	bool vinfo_stats = false;
	std::optional<size_t> vinfo_start, vinfo_end;
	sub_vcd_info->add_option("path,--file", vinfo_path, "Path to VCD file (auto-detected from testbench if omitted)");
	sub_vcd_info->add_option("--clock", vinfo_clock, "Reference clock signal name");
	sub_vcd_info->add_option("--scope", vinfo_scope, "Filter signals by hierarchical scope prefix");
	sub_vcd_info->add_option("-f,--filter", vinfo_filter, "Filter signal names by wildcard pattern (e.g. '*addr*')");
	sub_vcd_info->add_option("--max-signals", vinfo_max_signals, "Maximum number of signals to display (default: 100)");
	sub_vcd_info->add_flag("--stats", vinfo_stats, "Include First Toggle tick and Toggle Rate columns");
	sub_vcd_info->add_option("--start,--start-tick", vinfo_start, "Starting clock tick for toggle statistics");
	sub_vcd_info->add_option("--end,--end-tick", vinfo_end, "Ending clock tick for toggle statistics");

	// Subcommand: vcd-wave (vcd_wave)
	auto *sub_vcd_wave = app.add_subcommand("vcd-wave", "Sample and format clock-aligned VCD waveform tables");
	sub_vcd_wave->fallthrough();
	std::string vwave_path, vwave_instance, vwave_radix, vwave_clock, vwave_edge, vwave_gtkw;
	std::vector<std::string> vwave_signals;
	std::optional<size_t> vwave_start, vwave_end, vwave_max_rows;
	std::optional<uint64_t> vwave_start_time, vwave_end_time;
	bool vwave_no_collapse = false;
	sub_vcd_wave->add_option("path,-f,--file", vwave_path, "Path to VCD file (auto-detected if omitted), or instance/module name when -i/-s are omitted");
	sub_vcd_wave->add_option("-i,--instance,-m,--module", vwave_instance, "Module instance name/path or RTL module name to sample all I/O ports");
	sub_vcd_wave->add_option("-s,--signals", vwave_signals, "Signal names to sample (comma-separated or repeatable)");
	sub_vcd_wave->add_option("--radix,--format", vwave_radix, "Default multi-bit radix: 'hex', 'bin', or 'dec'");
	sub_vcd_wave->add_option("--clock", vwave_clock, "Clock signal to sample on");
	sub_vcd_wave->add_option("--edge", vwave_edge, "Clock edge: 'posedge' (default), 'negedge', or 'both'");
	sub_vcd_wave->add_option("--start,--start-tick", vwave_start, "Starting clock tick index");
	sub_vcd_wave->add_option("--end,--end-tick", vwave_end, "Ending clock tick index");
	sub_vcd_wave->add_option("--start-time", vwave_start_time, "Start time in VCD timescale units");
	sub_vcd_wave->add_option("--end-time", vwave_end_time, "End time in VCD timescale units");
	sub_vcd_wave->add_option("--max-rows,--cycles", vwave_max_rows, "Maximum rows to display (default: 50)");
	sub_vcd_wave->add_flag("--no-collapse", vwave_no_collapse, "Do not collapse consecutive idle clock cycles");
	sub_vcd_wave->add_option("--gtkw", vwave_gtkw, "Write GTKWave save file (.gtkw)");

	// Subcommand: vcd-search (vcd_search)
	auto *sub_vcd_search = app.add_subcommand("vcd-search", "Search VCD trace for clock cycles matching signal values");
	sub_vcd_search->fallthrough();
	std::string vsearch_path, vsearch_sig, vsearch_val, vsearch_clock, vsearch_edge;
	std::vector<std::string> vsearch_conds;
	std::optional<size_t> vsearch_limit, vsearch_start, vsearch_end;
	std::optional<uint64_t> vsearch_start_time, vsearch_end_time;
	bool vsearch_first = false, vsearch_last = false, vsearch_backward = false;
	sub_vcd_search->add_option("path,-f,--file", vsearch_path, "Path to VCD file (auto-detected if omitted)");
	sub_vcd_search->add_option("-w,--where,-c,--cond,--condition", vsearch_conds, "Signal match condition(s) 'sig=val' (comma-separated or repeatable)");
	sub_vcd_search->add_option("-s,--signal", vsearch_sig, "Signal name for single-signal search");
	sub_vcd_search->add_option("--value", vsearch_val, "Expected value for single-signal search");
	sub_vcd_search->add_option("--clock", vsearch_clock, "Clock signal to sample on");
	sub_vcd_search->add_option("--edge", vsearch_edge, "Clock edge: 'posedge', 'negedge', or 'both'");
	sub_vcd_search->add_flag("--backward", vsearch_backward, "Search backward from end of trace");
	sub_vcd_search->add_option("-l,--limit,--finding", vsearch_limit, "Maximum matched intervals to return (default: 10)");
	sub_vcd_search->add_flag("--first", vsearch_first, "Return only the first matched interval");
	sub_vcd_search->add_flag("--last", vsearch_last, "Return only the last matched interval");
	sub_vcd_search->add_option("--start,--start-tick", vsearch_start, "Starting clock tick index");
	sub_vcd_search->add_option("--end,--end-tick", vsearch_end, "Ending clock tick index");
	sub_vcd_search->add_option("--start-time", vsearch_start_time, "Start time in VCD timescale units");
	sub_vcd_search->add_option("--end-time", vsearch_end_time, "End time in VCD timescale units");

	// Subcommand: vcd-compare (vcd_compare)
	auto *sub_vcd_cmp = app.add_subcommand("vcd-compare", "Compare two VCD traces cycle-by-cycle for divergence");
	sub_vcd_cmp->fallthrough();
	std::string vcmp_a, vcmp_b, vcmp_scope, vcmp_clock, vcmp_edge;
	std::vector<std::string> vcmp_signals;
	std::optional<size_t> vcmp_start, vcmp_end;
	bool vcmp_strict = false;
	sub_vcd_cmp->add_option("path_a", vcmp_a, "First (golden/reference) VCD file (auto-detected if omitted)");
	sub_vcd_cmp->add_option("path_b", vcmp_b, "Second (candidate/test) VCD file (auto-detected if omitted)");
	sub_vcd_cmp->add_option("-s,--signals", vcmp_signals, "Optional signal names or wildcard patterns to compare");
	sub_vcd_cmp->add_option("--scope", vcmp_scope, "Optional scope prefix filter");
	sub_vcd_cmp->add_option("--clock", vcmp_clock, "Clock signal to sample on");
	sub_vcd_cmp->add_option("--edge", vcmp_edge, "Clock edge: 'posedge', 'negedge', or 'both'");
	sub_vcd_cmp->add_option("--start,--start-tick", vcmp_start, "Starting clock tick index");
	sub_vcd_cmp->add_option("--end,--end-tick", vcmp_end, "Ending clock tick index");
	sub_vcd_cmp->add_flag("--strict", vcmp_strict, "Fail if signal manifests differ between files");

	// Subcommand: vcd-trace (vcd_signal_to_code)
	auto *sub_vcd_trace = app.add_subcommand("vcd-trace", "Trace a VCD simulation signal to RTL declarations, drivers, and loads");
	sub_vcd_trace->fallthrough();
	std::string vtrace_sig, vtrace_vcd, vtrace_netlist, vtrace_top;
	std::vector<std::string> vtrace_pos;
	sub_vcd_trace->add_option("args", vtrace_pos, "Signal name (or [vcd] <signal>)");
	sub_vcd_trace->add_option("-s,--signal", vtrace_sig, "Simulation signal name (e.g. 'cpu8008.A_val')");
	sub_vcd_trace->add_option("-f,--file,-v,--vcd", vtrace_vcd, "Optional VCD file path");
	sub_vcd_trace->add_option("-n,--netlist", vtrace_netlist, "Optional hierarchy-preserving Yosys JSON netlist");
	sub_vcd_trace->add_option("--top", vtrace_top, "Optional top-level RTL module name");

	// Subcommand: vcd-labels (vcd_signal_labels)
	auto *sub_vcd_labels = app.add_subcommand("vcd-labels", "Register, query, or clear symbolic value labels for VCD signals");
	sub_vcd_labels->fallthrough();
	std::string vlbl_pos, vlbl_sig, vlbl_path;
	std::vector<std::string> vlbl_maps;
	bool vlbl_clear = false, vlbl_local = false, vlbl_list = false;
	sub_vcd_labels->add_option("target", vlbl_pos, "Optional VCD file path or signal name");
	sub_vcd_labels->add_option("-s,--signal", vlbl_sig, "Signal name or wildcard pattern (e.g. 'opcode', '*state*')");
	sub_vcd_labels->add_option("-m,--map,-l,--labels", vlbl_maps, "Value-to-label mappings 'val=label' (hex e.g. '0x3e=MVI_A' or binary with '?' wildcards e.g. 'b01?????" "?=MOV')");
	sub_vcd_labels->add_option("-f,--file", vlbl_path, "Optional VCD file path");
	sub_vcd_labels->add_flag("--list", vlbl_list, "List configured signal labels");
	sub_vcd_labels->add_flag("--clear", vlbl_clear, "Clear labels for the specified signal (or all signals if omitted)");
	sub_vcd_labels->add_flag("--local", vlbl_local, "Save to '<vcd>.labels.json' instead of project-wide 'vcd_labels.json'");
	sub_vcd_labels->footer(
		"Examples:\n"
		"  # Map exact hex values on 'opcode' (saved to project-wide vcd_labels.json):\n"
		"  turbostar-verilog vcd-labels -s opcode -l 0x00=NOP,0x3e=MVI_A,0x76=HLT\n\n"
		"  # Map binary bit-patterns with '?' don't-care wildcards (e.g. instruction families):\n"
		"  turbostar-verilog vcd-labels -s \"*opcode\" -l \"b01?????" "?=MOV,b10000??" "?=ADD,b00???110=MVI\"\n\n"
		"  # List or clear registered labels:\n"
		"  turbostar-verilog vcd-labels --list\n"
		"  turbostar-verilog vcd-labels -s opcode --clear"
	);

	// Subcommand: vcd-qualifier (vcd_signal_qualifier)
	auto *sub_vcd_qual = app.add_subcommand("vcd-qualifier", "Register, query, or clear validity qualifiers for VCD signals");
	sub_vcd_qual->fallthrough();
	std::string vqual_pos, vqual_sig, vqual_qual, vqual_path;
	std::vector<std::string> vqual_bindings;
	bool vqual_clear = false, vqual_local = false, vqual_list = false;
	sub_vcd_qual->add_option("target", vqual_pos, "Optional VCD file path or signal name");
	sub_vcd_qual->add_option("-s,--signal", vqual_sig, "Signal name or wildcard pattern to qualify");
	sub_vcd_qual->add_option("-q,--qualifier,--valid", vqual_qual, "1-bit qualifier signal (prefix '~' for active-low)");
	sub_vcd_qual->add_option("-b,--bindings", vqual_bindings, "Batch signal=qualifier bindings (comma-separated or repeatable)");
	sub_vcd_qual->add_option("-f,--file", vqual_path, "Optional VCD file path");
	sub_vcd_qual->add_flag("--list", vqual_list, "List configured validity qualifiers");
	sub_vcd_qual->add_flag("--clear", vqual_clear, "Clear qualifier for the specified signal (or all if omitted)");
	sub_vcd_qual->add_flag("--local", vqual_local, "Save to '<vcd>.labels.json' instead of project-wide 'vcd_labels.json'");

	CLI11_PARSE(app, argc, argv);
	const bool framed_tables = !no_frames;

	std::error_code ec;
	std::string cwd_str = fs::current_path(ec).string();
	if (cwd_str.empty()) {
		cwd_str = ".";
	}

	// Dispatch subcommands
	if (*sub_config) {
		bool has_fpga_opts = !cfg_target.empty() || !cfg_device.empty() || !cfg_package.empty() ||
		                     !cfg_top.empty() || (cfg_freq > 0.0) || !cfg_pcf.empty() ||
		                     !cfg_sv_path.empty() || !cfg_yosys_args.empty();
		bool has_tb_opts = !cfg_tb.empty() || !cfg_tb_modules.empty() || !cfg_tb_level.empty() ||
		                   cfg_tb_hier || cfg_tb_no_hier || cfg_tb_remove || cfg_tb_list || cfg_tb_scan;

		int rc = 0;
		if (has_fpga_opts || !has_tb_opts) {
			nlohmann::json j = nlohmann::json::object();
			j["action"] = has_fpga_opts ? "set" : "get";
			if (!cfg_target.empty()) j["target"] = cfg_target;
			if (!cfg_device.empty()) j["device"] = cfg_device;
			if (!cfg_package.empty()) j["package"] = cfg_package;
			if (!cfg_top.empty()) j["top_module"] = cfg_top;
			if (cfg_freq > 0.0) j["freq_mhz"] = cfg_freq;
			if (!cfg_pcf.empty()) j["pcf_file"] = cfg_pcf;
			if (!cfg_sv_path.empty()) j["sv_path"] = cfg_sv_path;
			if (!cfg_yosys_args.empty()) j["yosys_synth_args"] = cfg_yosys_args;
			rc = run_validator_tool<tools::verilog_project_settings_validator>(j, cwd_str, framed_tables);
		}
		if (has_tb_opts) {
			nlohmann::json j = nlohmann::json::object();
			if (!cfg_tb.empty()) j["testbench"] = cfg_tb;
			auto mods = split_csv_list(cfg_tb_modules);
			if (!mods.empty()) j["modules"] = mods;
			if (!cfg_tb_level.empty()) j["level"] = cfg_tb_level;
			if (cfg_tb_hier) j["hierarchical"] = true;
			else if (cfg_tb_no_hier) j["hierarchical"] = false;
			if (cfg_tb_scan) j["scan"] = true;
			if (cfg_tb_remove) j["remove"] = true;
			int tb_rc = run_validator_tool<tools::verilog_configure_testbench_validator>(j, cwd_str, framed_tables);
			if (tb_rc != 0) rc = tb_rc;
		}
		return rc;
	}

	if (*sub_compile) {
		nlohmann::json j = nlohmann::json::object();
		if (!cmp_module.empty()) j["module"] = cmp_module;
		if (cmp_lint_only) j["lint_only"] = true;
		if (cmp_synth_only) j["synth_only"] = true;
		if (!cmp_output.empty()) j["output_file"] = cmp_output;
		return run_validator_tool<tools::verilog_compile_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_sim) {
		nlohmann::json j = nlohmann::json::object();
		if (!sim_module.empty()) j["module"] = sim_module;
		if (!sim_tb.empty()) j["testbench"] = sim_tb;
		if (sim_hier) j["hierarchical"] = true;
		if (!sim_level.empty()) j["level"] = sim_level;
		if (!sim_top.empty()) j["top_module"] = sim_top;
		if (sim_timeout > 0) j["timeout_sec"] = sim_timeout;
		if (sim_quiet) j["summary_only"] = true;
		if (sim_verbose) j["verbose"] = true;
		return run_validator_tool<tools::verilog_simulate_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_info) {
		nlohmann::json j = nlohmann::json::object();
		if (!vinfo_path.empty()) j["path"] = vinfo_path;
		if (!vinfo_clock.empty()) j["clock_signal"] = vinfo_clock;
		if (!vinfo_scope.empty()) j["scope"] = vinfo_scope;
		if (!vinfo_filter.empty()) j["filter"] = vinfo_filter;
		if (vinfo_max_signals > 0) j["max_signals"] = vinfo_max_signals;
		if (vinfo_stats) j["stats"] = true;
		if (vinfo_start) j["start_tick"] = *vinfo_start;
		if (vinfo_end) j["end_tick"] = *vinfo_end;
		return run_validator_tool<tools::vcd_file_info_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_wave) {
		nlohmann::json j = nlohmann::json::object();
		auto sigs = split_csv_list(vwave_signals);
		if (!vwave_path.empty() && vwave_instance.empty() && sigs.empty() &&
		    !vwave_path.ends_with(".vcd") && !fs::exists(vwave_path, ec)) {
			vwave_instance = std::move(vwave_path);
			vwave_path.clear();
		}
		if (!vwave_path.empty()) j["path"] = vwave_path;
		if (!vwave_instance.empty()) j["instance"] = vwave_instance;
		if (!sigs.empty()) j["signals"] = sigs;
		if (!vwave_radix.empty()) j["radix"] = vwave_radix;
		if (!vwave_clock.empty()) j["clock_signal"] = vwave_clock;
		if (!vwave_edge.empty()) j["edge"] = vwave_edge;
		if (vwave_start) j["start_tick"] = *vwave_start;
		if (vwave_end) j["end_tick"] = *vwave_end;
		if (vwave_start_time) j["start_time"] = *vwave_start_time;
		if (vwave_end_time) j["end_time"] = *vwave_end_time;
		if (vwave_max_rows) j["max_rows"] = *vwave_max_rows;
		if (vwave_no_collapse) j["collapse_idle"] = false;
		if (!vwave_gtkw.empty()) j["gtkw_output"] = vwave_gtkw;
		return run_validator_tool<tools::vcd_wave_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_search) {
		nlohmann::json j = nlohmann::json::object();
		if (!vsearch_path.empty() && vsearch_path.find('=') != std::string::npos && !fs::exists(vsearch_path, ec)) {
			vsearch_conds.insert(vsearch_conds.begin(), vsearch_path);
			vsearch_path.clear();
		}
		if (!vsearch_path.empty()) j["path"] = vsearch_path;
		if (!vsearch_conds.empty()) {
			nlohmann::json cond_obj;
			std::string kv_err;
			if (!parse_kv_pairs(vsearch_conds, cond_obj, kv_err)) {
				std::cerr << std::format("Error: {}\n", kv_err);
				return 1;
			}
			j["conditions"] = std::move(cond_obj);
		}
		if (!vsearch_sig.empty()) j["signal"] = vsearch_sig;
		if (!vsearch_val.empty()) j["value"] = vsearch_val;
		if (!vsearch_clock.empty()) j["clock_signal"] = vsearch_clock;
		if (!vsearch_edge.empty()) j["edge"] = vsearch_edge;
		if (vsearch_backward) j["direction"] = "backward";
		if (vsearch_limit) j["limit"] = *vsearch_limit;
		if (vsearch_first) j["first"] = true;
		if (vsearch_last) j["last"] = true;
		if (vsearch_start) j["start_tick"] = *vsearch_start;
		if (vsearch_end) j["end_tick"] = *vsearch_end;
		if (vsearch_start_time) j["start_time"] = *vsearch_start_time;
		if (vsearch_end_time) j["end_time"] = *vsearch_end_time;
		return run_validator_tool<tools::vcd_search_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_cmp) {
		nlohmann::json j = nlohmann::json::object();
		if (!vcmp_a.empty()) j["path_a"] = vcmp_a;
		if (!vcmp_b.empty()) j["path_b"] = vcmp_b;
		auto sigs = split_csv_list(vcmp_signals);
		if (!sigs.empty()) j["signals"] = sigs;
		if (!vcmp_scope.empty()) j["scope"] = vcmp_scope;
		if (!vcmp_clock.empty()) j["clock_signal"] = vcmp_clock;
		if (!vcmp_edge.empty()) j["edge"] = vcmp_edge;
		if (vcmp_start) j["start_tick"] = *vcmp_start;
		if (vcmp_end) j["end_tick"] = *vcmp_end;
		if (vcmp_strict) j["strict"] = true;
		return run_validator_tool<tools::vcd_compare_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_trace) {
		for (const auto &p : vtrace_pos) {
			if (p.ends_with(".vcd") && vtrace_vcd.empty()) {
				vtrace_vcd = p;
			} else if (vtrace_sig.empty()) {
				vtrace_sig = p;
			}
		}
		nlohmann::json j = nlohmann::json::object();
		if (!vtrace_sig.empty()) j["signal"] = vtrace_sig;
		if (!vtrace_netlist.empty()) j["netlist"] = vtrace_netlist;
		return run_validator_tool<tools::vcd_signal_to_code_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_labels) {
		if (!vlbl_pos.empty()) {
			if ((vlbl_pos.ends_with(".vcd") || !vlbl_sig.empty()) && vlbl_path.empty()) {
				vlbl_path = vlbl_pos;
			} else if (vlbl_sig.empty()) {
				vlbl_sig = vlbl_pos;
			}
		}
		nlohmann::json j = nlohmann::json::object();
		if (!vlbl_path.empty()) j["path"] = vlbl_path;
		if (!vlbl_sig.empty()) j["signal"] = vlbl_sig;
		if (!vlbl_maps.empty()) {
			nlohmann::json lbl_obj;
			std::string kv_err;
			if (!parse_kv_pairs(vlbl_maps, lbl_obj, kv_err)) {
				std::cerr << std::format("Error: {}\n", kv_err);
				return 1;
			}
			j["labels"] = std::move(lbl_obj);
		}
		if (vlbl_clear) j["clear"] = true;
		if (vlbl_local) j["global"] = false;
		return run_validator_tool<tools::vcd_signal_labels_validator>(j, cwd_str, framed_tables);
	}

	if (*sub_vcd_qual) {
		if (!vqual_pos.empty()) {
			if ((vqual_pos.ends_with(".vcd") || !vqual_sig.empty()) && vqual_path.empty()) {
				vqual_path = vqual_pos;
			} else if (vqual_sig.empty()) {
				vqual_sig = vqual_pos;
			}
		}
		nlohmann::json j = nlohmann::json::object();
		if (!vqual_path.empty()) j["path"] = vqual_path;
		if (!vqual_sig.empty()) j["signal"] = vqual_sig;
		if (!vqual_qual.empty()) j["qualifier"] = vqual_qual;
		if (!vqual_bindings.empty()) {
			nlohmann::json bind_obj;
			std::string kv_err;
			if (!parse_kv_pairs(vqual_bindings, bind_obj, kv_err)) {
				std::cerr << std::format("Error: {}\n", kv_err);
				return 1;
			}
			j["bindings"] = std::move(bind_obj);
		}
		if (vqual_clear) j["clear"] = true;
		if (vqual_local) j["global"] = false;
		return run_validator_tool<tools::vcd_signal_qualifier_validator>(j, cwd_str, framed_tables);
	}
	if (dry_run) {
		mode_update = true;
	}
	if (!untrusted_timing_report.empty()) {
		mode_pnr = true;
	}

	if (show_code < 0) {
		show_code = 0;
	} else if (show_code > 50) {
		show_code = 50;
	}
	if (top_k == 0) {
		top_k = 3;
	}

	// Load FPGA project settings from current directory if present
	verilog::fpga_target_config cfg;
	std::string cfg_err;
	bool has_cfg = verilog::fpga_project_settings::load_from_project(cwd_str, cfg, cfg_err);

	if (untrusted_target.empty()) {
		if (has_cfg && cfg.target == "ice40") {
			untrusted_target = "iCE40";
		} else {
			untrusted_target = "Generic LUT4";
		}
	}

	std::string safe_target = untrusted_target;
	if (safe_target != "Generic LUT4" && safe_target != "Generic LUT6" &&
	    safe_target != "iCE40" && safe_target != "Generic Gates") {
		std::cerr << std::format("Error: Unsupported target '{}'. Allowed: 'Generic LUT4', 'Generic LUT6', 'iCE40', 'Generic Gates'\n",
		                         safe_target);
		return 1;
	}

	std::string safe_netlist;
	if (!untrusted_netlist.empty()) {
		fs::path netlist_fs_path(untrusted_netlist);
		if (!fs::is_regular_file(netlist_fs_path, ec)) {
			std::cerr << std::format("Error: Netlist file '{}' does not exist or is not a regular file.\n", untrusted_netlist);
			return 1;
		}
		safe_netlist = fs::weakly_canonical(netlist_fs_path, ec).string();
	}

	std::string safe_timing_report;
	if (!untrusted_timing_report.empty()) {
		fs::path tr_fs_path(untrusted_timing_report);
		if (!fs::is_regular_file(tr_fs_path, ec)) {
			std::cerr << std::format("Error: Timing report '{}' does not exist or is not a regular file.\n", untrusted_timing_report);
			return 1;
		}
		safe_timing_report = fs::weakly_canonical(tr_fs_path, ec).string();
	}

	std::string clean_module;
	if (!untrusted_module.empty()) {
		if (!fs_utils::is_safe_for_ui(untrusted_module)) {
			std::cerr << "Error: Invalid characters in --module argument.\n";
			return 1;
		}
		clean_module = untrusted_module;
	}

	// If positional 'path' was given and is not an existing directory in non-update mode,
	// check if it's a module name or a single .sv/.v file
	std::string safe_file;
	if (!untrusted_path.empty()) {
		fs::path p_cand(untrusted_path);
		if (fs::exists(p_cand, ec)) {
			if (fs::is_regular_file(p_cand, ec)) {
				safe_file = fs::weakly_canonical(p_cand, ec).string();
			}
		} else if (!mode_update && clean_module.empty()) {
			if (!fs_utils::is_safe_for_ui(untrusted_path)) {
				std::cerr << "Error: Invalid characters in path/module argument.\n";
				return 1;
			}
			clean_module = untrusted_path;
			untrusted_path.clear();
		} else {
			std::cerr << std::format("Error: Path '{}' does not exist.\n", untrusted_path);
			return 1;
		}
	}

	if (safe_file.empty() && !clean_module.empty()) {
		fs::path m_cand(clean_module);
		if (fs::exists(m_cand, ec) && fs::is_regular_file(m_cand, ec)) {
			safe_file = fs::weakly_canonical(m_cand, ec).string();
		}
	}

	// =========================================================================
	// MODE 1: Physical Place-and-Route Timing (--pnr)
	// =========================================================================
	if (mode_pnr) {
		verilog::nextpnr_timing_analyzer pnr_analyzer;
		std::string err;

		if (!safe_timing_report.empty()) {
			if (!pnr_analyzer.load_from_files(safe_timing_report, safe_netlist, err)) {
				std::cerr << std::format("Error loading timing report '{}': {}\n", safe_timing_report, err);
				return 1;
			}
			std::string md = pnr_analyzer.generate_timing_markdown(
				!all_paths, !no_group, top_k, !no_collapse, show_segments || verbose, show_code);
			std::cout << format_for_terminal(md, framed_tables);
			return 0;
		}

		auto sys_tools = verilog::fpga_project_settings::detect_system_tools();
		auto pcfs = verilog::fpga_project_settings::scan_pcf_files(cwd_str);
		if (cfg.target.empty() && sys_tools.available_targets.size() == 1) {
			cfg.target = sys_tools.available_targets.front();
		}
		std::string configured_top = cfg.top_module;
		if (!clean_module.empty() && cfg.top_module.empty()) {
			cfg.top_module = clean_module;
		}

		std::vector<std::string> missing;
		if (!verilog::fpga_project_settings::is_complete(cfg, missing)) {
			std::cout << format_for_terminal(
				verilog::fpga_project_settings::format_guidance(cwd_str, cfg, sys_tools, pcfs), framed_tables);
			return 1;
		}
		if (cfg.pcf_file.empty() && pcfs.size() == 1) {
			cfg.pcf_file = pcfs.front();
		}

		fs::path scan_root = cwd_str;
		if (!untrusted_path.empty() && fs::is_directory(untrusted_path, ec)) {
			scan_root = fs::weakly_canonical(untrusted_path, ec);
		} else if (!safe_file.empty()) {
			scan_root = fs::path(safe_file).parent_path();
			std::string fname = scan_root.filename().string();
			if (scan_root.has_parent_path() && fname != "sv" && fname != "src" && fname != "rtl" &&
			    (cfg.sv_path.empty() || fname != cfg.sv_path) && !fs::equivalent(scan_root, fs::path(cwd_str), ec)) {
				scan_root = scan_root.parent_path();
			}
		} else if (!cfg.sv_path.empty()) {
			scan_root = fs::path(cwd_str) / cfg.sv_path;
		}

		std::vector<std::string> sources = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
		if (sources.empty() && !safe_file.empty()) {
			sources.push_back(safe_file);
		}
		if (sources.empty()) {
			std::cerr << "Error: No synthesizable SystemVerilog/Verilog source files found.\n";
			return 1;
		}

		std::string submodule_banner;
		if (!clean_module.empty()) {
			std::string resolved_mod = verilog::verilog_module_scanner::resolve_module_name(clean_module, safe_file, sources);
			if (!resolved_mod.empty()) {
				cfg.top_module = resolved_mod;
				if (!configured_top.empty() && resolved_mod != configured_top) {
					cfg.pcf_file.clear();
					submodule_banner = std::format(
						"> [!NOTE] **Out-of-Context Submodule PnR (`{}`)**: Analyzed submodule `{}` with unconstrained I/O placement (configured project `top_module` is `{}`).\n\n",
						resolved_mod, resolved_mod, configured_top);
				}
			}
		}

		if (!pnr_analyzer.synthesize_and_route(cfg, sources, err)) {
			std::cerr << std::format("On-demand synthesis and place-and-route failed: {}\n", err);
			return 1;
		}

		std::string md = submodule_banner + pnr_analyzer.generate_timing_markdown(
			!all_paths, !no_group, top_k, !no_collapse, show_segments || verbose, show_code);
		std::cout << format_for_terminal(md, framed_tables);
		return 0;
	}

	// Gather synthesizable RTL files for Mode 2 (--update) and Mode 3 (default logic_depth)
	if (untrusted_path.empty()) {
		if (has_cfg && !cfg.sv_path.empty()) {
			untrusted_path = cfg.sv_path;
		} else {
			untrusted_path = ".";
		}
	}

	fs::path input_fs_path(untrusted_path);
	if (!fs::exists(input_fs_path, ec)) {
		std::cerr << std::format("Error: Path '{}' does not exist.\n", untrusted_path);
		return 1;
	}
	std::string safe_path = fs::weakly_canonical(input_fs_path, ec).string();

	std::vector<std::string> sv_files;
	try {
		if (!safe_file.empty() && !mode_update) {
			fs::path scan_root = fs::path(safe_file).parent_path();
			std::string fname = scan_root.filename().string();
			if (scan_root.has_parent_path() && fname != "sv" && fname != "src" && fname != "rtl" &&
			    (cfg.sv_path.empty() || fname != cfg.sv_path) && !fs::equivalent(scan_root, fs::path(cwd_str), ec)) {
				scan_root = scan_root.parent_path();
			}
			sv_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
			if (sv_files.empty()) {
				sv_files.push_back(safe_file);
			}
		} else {
			sv_files = verilog::verilog_module_scanner::find_synthesizable_files(safe_path);
		}
	} catch (const std::exception &e) {
		std::cerr << std::format("Error scanning path '{}': {}\n", safe_path, e.what());
		return 1;
	}

	if (sv_files.empty() && safe_netlist.empty()) {
		std::cerr << std::format("Error: No synthesizable Verilog/SystemVerilog files found in '{}'\n", safe_path);
		return 1;
	}

	// =========================================================================
	// MODE 2: Batch Update In-Place Port Timing Annotations (-u / --update)
	// =========================================================================
	if (mode_update) {
		auto declared_modules = verilog::verilog_module_scanner::discover_modules_in_files(sv_files);
		if (declared_modules.empty()) {
			std::cerr << std::format("Error: No module declarations found across {} scanned files in '{}'\n",
			                         sv_files.size(), safe_path);
			return 1;
		}

		struct module_result {
			std::string module_name;
			std::string source_file;
			size_t reg_depth{0};
			size_t luts{0};
			size_t ffs{0};
			size_t inputs{0};
			size_t outputs{0};
			size_t annotated_ports{0};
			bool success{false};
			std::string err;
		};

		std::vector<module_result> results;
		size_t current_idx = 0;

		for (const auto &decl : declared_modules) {
			++current_idx;
			if (!quiet) {
				std::cout << std::format("[{}/{}] Analyzing module '{}'...\n", current_idx, declared_modules.size(), decl.module_name);
			}

			verilog::netlist_graph_analyzer analyzer;
			std::string err;
			bool loaded = false;

			if (!safe_netlist.empty()) {
				loaded = analyzer.load_from_file(safe_netlist, err, decl.module_name);
			} else {
				loaded = analyzer.synthesize_and_load(decl.module_name, sv_files, safe_target, err);
			}

			module_result res;
			res.module_name = decl.module_name;
			res.source_file = decl.source_file;

			if (!loaded) {
				res.success = false;
				res.err = err;
				if (verbose) {
					std::cerr << std::format("  Failed to synthesize module '{}': {}\n", decl.module_name, err);
				}
			} else {
				res.reg_depth = analyzer.get_max_reg_to_reg_depth();
				const auto &area = analyzer.get_area_summary();
				res.luts = (safe_target == "Generic Gates") ? (area.other_comb_count + area.lut_count) : area.lut_count;
				res.ffs = area.ff_count;
				res.inputs = analyzer.get_input_ports().size();
				res.outputs = analyzer.get_output_ports().size();

				if (dry_run) {
					res.success = true;
				} else {
					std::string up_err;
					if (analyzer.update_verilog_file_annotations(res.source_file, res.annotated_ports, up_err)) {
						res.success = true;
					} else {
						res.err = up_err;
					}
				}
			}

			results.push_back(std::move(res));
		}

		size_t success_count = 0;
		for (const auto &r : results) {
			if (r.success) {
				++success_count;
			}
		}

		if (!quiet) {
			std::string table_md = "| Module | Reg->Reg Depth | LUTs | FFs | Inputs | Outputs | Status | Source File |\n"
			                       "| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |\n";

			for (const auto &r : results) {
				std::string depth_str = r.success ? std::format("{} LUTs", r.reg_depth) : "-";
				std::string lut_str = r.success ? std::format("{}", r.luts) : "-";
				std::string ff_str = r.success ? std::format("{}", r.ffs) : "-";
				std::string in_str = r.success ? std::format("{}", r.inputs) : "-";
				std::string out_str = r.success ? std::format("{}", r.outputs) : "-";
				std::string status_str = r.success ? (dry_run ? "DRY-RUN" : "OK") : "FAILED";
				std::string rel_source = verilog::nextpnr_timing_analyzer::clean_display_location(r.source_file, cwd_str);

				table_md += std::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n",
				                        r.module_name, depth_str, lut_str, ff_str, in_str, out_str, status_str, rel_source);
			}
			std::cout << "\n" << format_for_terminal(table_md, framed_tables) << "\n";
			std::cout << std::format("{}: {}/{} modules processed (Target: {})\n",
			                         dry_run ? "Dry run completed" : "Summary",
			                         success_count, results.size(), safe_target);
		}
		return (success_count == results.size()) ? 0 : 1;
	}

	// =========================================================================
	// MODE 3 (Default): Single-Module Logic Depth & Area Report
	// =========================================================================
	verilog::netlist_graph_analyzer analyzer;
	std::string err;

	if (!safe_netlist.empty()) {
		if (!analyzer.load_from_file(safe_netlist, err, clean_module)) {
			std::cerr << std::format("Error loading netlist '{}': {}\n", safe_netlist, err);
			return 1;
		}
	} else {
		std::string resolved_module = verilog::verilog_module_scanner::resolve_module_name(
			clean_module, safe_file, sv_files);
		if (resolved_module.empty()) {
			if (!cfg.top_module.empty()) {
				resolved_module = cfg.top_module;
			} else {
				auto discovered = verilog::verilog_module_scanner::discover_modules_in_files(sv_files);
				if (discovered.size() == 1) {
					resolved_module = discovered.front().module_name;
				} else {
					std::string mod_list;
					for (size_t i = 0; i < discovered.size(); ++i) {
						if (i > 0) mod_list += ", ";
						mod_list += std::format("{} ({})", discovered[i].module_name,
						                        fs::path(discovered[i].source_file).filename().string());
					}
					std::cerr << "Error: Multiple modules found and no module specified (nor top_module in turbostar.json).\n"
					          << "Pass -m <module> (or -u to batch-update port timing annotations in .sv files).\n";
					if (!mod_list.empty()) {
						std::cerr << std::format("Available modules: {}\n", mod_list);
					}
					return 1;
				}
			}
		}
		if (!analyzer.synthesize_and_load(resolved_module, sv_files, safe_target, err)) {
			std::cerr << std::format("Synthesis/analysis failed for module '{}': {}\n", resolved_module, err);
			return 1;
		}
	}

	std::string base_dir = cwd_str;
	if (!safe_file.empty()) {
		fs::path sdir = fs::path(safe_file).parent_path();
		if ((sdir.filename() == "sv" || sdir.filename() == "src" || sdir.filename() == "rtl") && sdir.has_parent_path()) {
			sdir = sdir.parent_path();
		}
		base_dir = sdir.string();
	} else if (!safe_netlist.empty()) {
		fs::path ndir = fs::path(safe_netlist).parent_path();
		if ((ndir.filename() == "synth" || ndir.filename() == "build") && ndir.has_parent_path()) {
			ndir = ndir.parent_path();
		}
		base_dir = ndir.string();
	} else if (fs::is_directory(safe_path, ec)) {
		fs::path pdir(safe_path);
		if ((pdir.filename() == "sv" || pdir.filename() == "src" || pdir.filename() == "rtl") && pdir.has_parent_path()) {
			pdir = pdir.parent_path();
		}
		base_dir = pdir.string();
	}

	std::string report_md = analyzer.generate_logic_depth_report_markdown(
		safe_target, top_k, !no_group, !no_collapse, show_code, base_dir);
	std::cout << format_for_terminal(report_md, framed_tables);
	return 0;
}
