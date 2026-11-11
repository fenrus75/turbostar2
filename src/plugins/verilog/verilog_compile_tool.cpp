#include "plugins/verilog/verilog_compile_tool.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <sstream>
#include <vector>
#include "fs_utils.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_module_scanner.h"

namespace tools
{

namespace fs = std::filesystem;

namespace
{

size_t count_log_lines(std::string_view text) noexcept
{
	if (text.empty()) {
		return 0;
	}
	size_t lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
	if (text.back() != '\n') {
		++lines;
	}
	return lines;
}

bool write_compile_log(agentlib::tool_context &ctx, const std::string &safe_out_path, const std::string &content,
                       std::string &out_err)
{
	if (safe_out_path.find("://") != std::string::npos) {
		if (safe_out_path.starts_with("file://")) {
			std::string disk_path = safe_out_path.substr(7);
			std::string dir_note;
			fs_utils::ensure_parent_directory_exists(disk_path, dir_note);
			std::ofstream ofs(disk_path, std::ios::binary | std::ios::trunc);
			if (!ofs.is_open()) {
				out_err = "Failed to open log file for writing: " + disk_path;
				return false;
			}
			ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
			return true;
		}

		auto *vfs = ctx.fs_security.get_vfs();
		if (vfs != nullptr) {
			std::string desc = vfs->write_file(safe_out_path, content.data(), content.size());
			if (!desc.empty()) {
				return true;
			}
		}

		if (safe_out_path.starts_with("tmp://")) {
			std::string rel = safe_out_path.substr(6);
			fs::path disk_tmp = fs::path(fs_utils::get_project_tmp_dir()) / rel;
			std::error_code ec;
			if (disk_tmp.has_parent_path()) {
				fs::create_directories(disk_tmp.parent_path(), ec);
			}
			std::ofstream ofs(disk_tmp, std::ios::binary | std::ios::trunc);
			if (!ofs.is_open()) {
				out_err = "Failed to write temporary log file: " + disk_tmp.string();
				return false;
			}
			ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
			return true;
		}

		out_err = "Failed to write log to VFS URI: " + safe_out_path;
		return false;
	}

	std::string dir_note;
	fs_utils::ensure_parent_directory_exists(safe_out_path, dir_note);
	std::ofstream ofs(safe_out_path, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open()) {
		out_err = "Failed to open log file for writing: " + safe_out_path;
		return false;
	}
	ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
	return true;
}

struct parsed_verilator_diagnostics {
	std::vector<std::string> errors;
	std::vector<std::string> correctness_warnings;
	std::vector<std::string> style_warnings;
	std::map<std::string, std::vector<std::string>> style_by_code;
};

constexpr std::string_view extract_verilator_warning_code(std::string_view line) noexcept
{
	constexpr std::string_view prefix = "%Warning-";
	if (!line.starts_with(prefix)) {
		return "";
	}
	std::string_view rest = line.substr(prefix.size());
	size_t colon = rest.find(':');
	if (colon == std::string_view::npos || colon == 0) {
		return "";
	}
	return rest.substr(0, colon);
}

constexpr bool is_verilator_style_code(std::string_view code) noexcept
{
	return code == "EOFNEWLINE" || code == "UNUSEDSIGNAL" || code == "UNUSEDPARAM" ||
	       code == "UNUSED" || code == "UNUSEDGENVAR" || code == "PINCONNECTEMPTY" ||
	       code == "PINNOCONNECT" || code == "DECLFILENAME" || code == "VARHIDDEN" ||
	       code == "DEFPARAM" || code == "INCABSPATH" || code == "TIMESCALEMOD";
}

parsed_verilator_diagnostics parse_verilator_output(std::string_view raw_out)
{
	parsed_verilator_diagnostics res;
	std::set<std::string> seen_err;
	std::set<std::string> seen_warn;

	std::istringstream iss{std::string(raw_out)};
	std::string line;
	while (std::getline(iss, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.starts_with("%Error")) {
			if (line.find("Exiting due to") == std::string::npos && seen_err.insert(line).second) {
				res.errors.push_back(line);
			}
		} else if (line.starts_with("%Warning")) {
			if (seen_warn.insert(line).second) {
				std::string_view code = extract_verilator_warning_code(line);
				if (is_verilator_style_code(code)) {
					res.style_warnings.push_back(line);
					res.style_by_code[std::string(code)].push_back(line);
				} else {
					res.correctness_warnings.push_back(line);
				}
			}
		}
	}
	return res;
}

std::string format_categorized_warnings(const parsed_verilator_diagnostics &v_diag,
                                        std::span<const std::string> yosys_warnings,
                                        std::span<const std::string> pnr_warnings,
                                        std::string_view log_file)
{
	std::string out;
	size_t corr_total = v_diag.correctness_warnings.size() + yosys_warnings.size() + pnr_warnings.size();

	if (corr_total > 0) {
		out += std::format("\n## Correctness & Synthesis Warnings ({})\n\n", corr_total);
		size_t shown = 0;
		constexpr size_t max_corr_shown = 20;
		for (const auto &w : v_diag.correctness_warnings) {
			if (shown >= max_corr_shown) break;
			out += std::format("- [Verilator] `{}`\n", w);
			++shown;
		}
		for (const auto &w : yosys_warnings) {
			if (shown >= max_corr_shown) break;
			out += std::format("- [Yosys] `{}`\n", w);
			++shown;
		}
		for (const auto &w : pnr_warnings) {
			if (shown >= max_corr_shown) break;
			out += std::format("- [nextpnr] `{}`\n", w);
			++shown;
		}
		if (corr_total > shown) {
			out += std::format("- *(...and {} more correctness/synthesis warning(s) in `{}`)*\n",
			                   corr_total - shown, log_file);
		}
	}

	if (!v_diag.style_by_code.empty()) {
		out += std::format("\n## Style & Unused Resource Notes ({})\n\n", v_diag.style_warnings.size());
		for (const auto &[code, items] : v_diag.style_by_code) {
			if (items.empty()) continue;
			if (items.size() == 1) {
				out += std::format("- **`{}`** (1): `{}`\n", code, items.front());
			} else {
				out += std::format("- **`{}`** ({} occurrences) — e.g. `{}` *(+{} more in `{}`)*\n",
				                   code, items.size(), items.front(), items.size() - 1, log_file);
			}
		}
	}

	return out;
}

struct parsed_yosys_diagnostics {
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
	std::vector<std::string> latch_notes;
	std::vector<std::string> loop_notes;
	std::string stat_block;
};

bool is_yosys_loop_diagnostic(std::string_view line) noexcept
{
	if (line.find("Executing SCC pass") != std::string_view::npos ||
	    line.find("Generating RTLIL representation") != std::string_view::npos ||
	    line.find("SCC_BREAKER") != std::string_view::npos) {
		return false;
	}
	if (line.find("Found logic loop") != std::string_view::npos ||
	    line.find("Found an SCC:") != std::string_view::npos) {
		return true;
	}
	size_t found_pos = line.find("Found ");
	if (found_pos != std::string_view::npos &&
	    (line.find("SCC", found_pos) != std::string_view::npos ||
	     line.find("scc", found_pos) != std::string_view::npos)) {
		size_t num_pos = found_pos + 6;
		if (num_pos < line.size() && line[num_pos] >= '1' && line[num_pos] <= '9') {
			return true;
		}
	}
	return false;
}

parsed_yosys_diagnostics parse_yosys_output(std::string_view raw_out, std::string_view target_mod)
{
	parsed_yosys_diagnostics res;
	std::set<std::string> seen_err;
	std::set<std::string> seen_warn;

	std::istringstream iss{std::string(raw_out)};
	std::string line;
	bool in_stat_block = false;
	std::string target_header = std::format("=== {} ===", target_mod);
	std::vector<std::string> stat_lines;

	while (std::getline(iss, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line.starts_with("ERROR:")) {
			if (seen_err.insert(line).second) {
				res.errors.push_back(line);
			}
		} else if (line.starts_with("Warning:")) {
			if (seen_warn.insert(line).second) {
				res.warnings.push_back(line);
			}
		}

		if (line.find("Latch inferred for signal") != std::string::npos &&
		    line.find("No latch inferred") == std::string::npos) {
			std::string trimmed = line;
			while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
				trimmed.erase(trimmed.begin());
			}
			if (!trimmed.empty() && res.latch_notes.size() < 10) {
				res.latch_notes.push_back(trimmed);
			}
		}

		if (is_yosys_loop_diagnostic(line)) {
			std::string trimmed = line;
			while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
				trimmed.erase(trimmed.begin());
			}
			if (!trimmed.empty()) {
				res.loop_notes.push_back(trimmed);
			}
		}

		if (line == target_header || line == "=== design hierarchy ===") {
			in_stat_block = (line == target_header);
			if (in_stat_block) {
				stat_lines.clear();
			}
		} else if (in_stat_block) {
			if (line.starts_with("===") || (line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0])) && line.find('.') != std::string::npos)) {
				in_stat_block = false;
			} else if (!line.empty()) {
				stat_lines.push_back(line);
				if (res.latch_notes.empty() &&
				    (line.find("$dlatch") != std::string::npos || line.find("$_DLATCH_") != std::string::npos)) {
					std::string trimmed = line;
					while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
						trimmed.erase(trimmed.begin());
					}
					if (!trimmed.empty()) {
						res.latch_notes.push_back("Netlist cell: " + trimmed);
					}
				}
			}
		}
	}

	if (!stat_lines.empty()) {
		std::ostringstream oss;
		for (const auto &sl : stat_lines) {
			oss << sl << "\n";
		}
		res.stat_block = oss.str();
	}

	return res;
}

struct parsed_pnr_diagnostics {
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
	std::vector<std::string> utilization;
};

parsed_pnr_diagnostics parse_nextpnr_output(std::string_view raw_out)
{
	parsed_pnr_diagnostics res;
	std::set<std::string> seen_err;
	std::set<std::string> seen_warn;

	std::istringstream iss{std::string(raw_out)};
	std::string line;
	bool in_util = false;

	while (std::getline(iss, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line.starts_with("ERROR:") || line.find("ERROR:") != std::string::npos) {
			if (seen_err.insert(line).second) {
				res.errors.push_back(line);
			}
		} else if (line.starts_with("Warning:") || line.find("Warning:") != std::string::npos) {
			if (seen_warn.insert(line).second) {
				res.warnings.push_back(line);
			}
		}

		if (line.find("Device utilisation:") != std::string::npos) {
			in_util = true;
			continue;
		}
		if (in_util) {
			size_t colon = line.find("Info:");
			std::string payload = (colon != std::string::npos) ? line.substr(colon + 5) : line;
			while (!payload.empty() && (payload.front() == ' ' || payload.front() == '\t')) {
				payload.erase(payload.begin());
			}
			if (payload.empty()) {
				in_util = false;
			} else if (payload.find(':') != std::string::npos && payload.find('/') != std::string::npos) {
				res.utilization.push_back(payload);
			} else {
				in_util = false;
			}
		}
	}

	return res;
}

} // namespace

verilog_compile_tool::verilog_compile_tool(verilog_compile_args args)
	: agentlib::llm_tool_action("Compiling and verifying Verilog/SystemVerilog design"),
	  args_(std::move(args))
{
}

bool verilog_compile_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string verilog_compile_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = ".";
	}

	verilog::fpga_target_config cfg;
	std::string load_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, load_err);

	auto sys_tools = verilog::fpga_project_settings::detect_system_tools();
	auto pcfs = verilog::fpga_project_settings::scan_pcf_files(project_root);

	if (cfg.target.empty() && sys_tools.available_targets.size() == 1) {
		cfg.target = sys_tools.available_targets.front();
	}
	if (cfg.pcf_file.empty() && pcfs.size() == 1) {
		cfg.pcf_file = pcfs.front();
	}

	// Gather synthesizable RTL files
	fs::path scan_root;
	if (!cfg.sv_path.empty()) {
		scan_root = fs::path(project_root) / cfg.sv_path;
	} else {
		scan_root = fs::path(project_root);
	}

	std::vector<std::string> sources = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
	if (sources.empty() && !args_.safe_file.empty()) {
		sources.push_back(args_.safe_file);
	}

	if (sources.empty()) {
		set_failure(ctx, "No synthesizable RTL source files found");
		return "Error: No synthesizable SystemVerilog/Verilog source files found in workspace.";
	}

	// Resolve target module name
	std::string target_mod;
	if (!args_.requested_module.empty()) {
		target_mod = verilog::verilog_module_scanner::resolve_module_name(args_.requested_module, args_.safe_file, sources);
		if (target_mod.empty()) {
			target_mod = args_.requested_module;
		}
	} else if (!cfg.top_module.empty()) {
		target_mod = cfg.top_module;
	} else {
		std::string resolved_main = verilog::verilog_module_scanner::resolve_module_name("main", "", sources);
		if (!resolved_main.empty() && resolved_main == "main") {
			target_mod = resolved_main;
		} else {
			std::string resolved_top = verilog::verilog_module_scanner::resolve_module_name("top", "", sources);
			if (!resolved_top.empty() && resolved_top == "top") {
				target_mod = resolved_top;
			} else {
				auto mods = verilog::verilog_module_scanner::discover_modules_in_files(sources);
				target_mod = mods.empty() ? "main" : mods.front().module_name;
			}
		}
	}

	bool is_top_module = cfg.top_module.empty() || (target_mod == cfg.top_module);

	// Filter RTL sources to the transitive submodule hierarchy of target_mod (plus any package-only files)
	// so compiling a leaf module does not parse or emit warnings from unrelated modules in the workspace.
	auto all_mods = verilog::verilog_module_scanner::discover_modules_in_files(sources);
	auto hier_mods = verilog::verilog_module_scanner::discover_submodule_hierarchy(target_mod, sources);
	std::set<std::string> hier_mod_set(hier_mods.begin(), hier_mods.end());

	std::set<std::string> files_with_any_module;
	std::set<std::string> files_with_hier_module;
	for (const auto &dm : all_mods) {
		files_with_any_module.insert(dm.source_file);
		if (hier_mod_set.contains(dm.module_name)) {
			files_with_hier_module.insert(dm.source_file);
		}
	}

	std::vector<std::string> active_sources;
	if (!files_with_hier_module.empty()) {
		for (const auto &src : sources) {
			if (files_with_hier_module.contains(src) || !files_with_any_module.contains(src)) {
				active_sources.push_back(src);
			}
		}
	} else if (!args_.safe_file.empty()) {
		active_sources.push_back(args_.safe_file);
		for (const auto &src : sources) {
			if (!files_with_any_module.contains(src) && src != args_.safe_file) {
				active_sources.push_back(src);
			}
		}
	} else {
		active_sources = sources;
	}

	std::string scope_summary;
	if (active_sources.size() == sources.size()) {
		scope_summary = std::format("`{}` ({} RTL file{} in hierarchy)",
		                            target_mod, active_sources.size(), active_sources.size() == 1 ? "" : "s");
	} else {
		scope_summary = std::format("`{}` ({} of {} workspace RTL files in module hierarchy)",
		                            target_mod, active_sources.size(), sources.size());
	}

	std::string verilator_bin = fs_utils::find_executable("verilator");
	std::string yosys_bin = fs_utils::find_executable("yosys");

	if (args_.lint_only && verilator_bin.empty()) {
		set_failure(ctx, "verilator not found in PATH");
		return "Error: `verilator` executable was not found in system PATH (required for `lint_only=true`).";
	}
	if (!args_.lint_only && yosys_bin.empty()) {
		set_failure(ctx, "yosys not found in PATH");
		return "Error: `yosys` executable was not found in system PATH.";
	}

	std::string combined_raw_log;
	auto t_total_start = std::chrono::steady_clock::now();

	// =========================================================================
	// Stage 1: Verilator Lint
	// =========================================================================
	parsed_verilator_diagnostics v_diag;
	bool verilator_ran = false;
	if (!verilator_bin.empty()) {
		verilator_ran = true;
		std::string v_cmd = std::format("{} --lint-only -Wall --Wno-fatal -Wno-DECLFILENAME -Wno-EOFNEWLINE --timing --top-module {}",
		                                fs_utils::escape_shell_arg(verilator_bin),
		                                fs_utils::escape_shell_arg(target_mod));
		for (const auto &src : active_sources) {
			v_cmd += " " + fs_utils::escape_shell_arg(src);
		}
		std::string v_out = fs_utils::execute_command_sync(v_cmd);
		combined_raw_log += "================================================================================\n";
		combined_raw_log += "=== Stage 1: Verilator Lint (" + target_mod + ")\n";
		combined_raw_log += "=== Command: " + v_cmd + "\n";
		combined_raw_log += "================================================================================\n";
		combined_raw_log += v_out.empty() ? "(clean — no output)\n\n" : (v_out + "\n");

		v_diag = parse_verilator_output(v_out);

		if (!v_diag.errors.empty()) {
			std::string log_err;
			write_compile_log(ctx, args_.safe_output_file, combined_raw_log, log_err);
			size_t total_lines = count_log_lines(combined_raw_log);

			std::string out = std::format("# Verilog Compile Report: `{}`\n\n", target_mod);
			out += "- **Status**: **FAILED** (Verilator Syntax / Elaboration Error)\n";
			out += std::format("- **Target Scope**: {}\n", scope_summary);
			out += std::format("- **Full Log**: `{}` ({} lines, {} bytes)\n\n",
			                   args_.requested_output_file, total_lines, combined_raw_log.size());
			out += "## Errors\n\n";
			for (const auto &e : v_diag.errors) {
				out += std::format("- `{}`\n", e);
			}
			out += format_categorized_warnings(v_diag, {}, {}, args_.requested_output_file);
			out += std::format("\n> **Tip**: Inspect full context in `{}` using `fs_read_lines(path=\"{}\", start_line=1, end_line=100)`.\n",
			                   args_.requested_output_file, args_.requested_output_file);
			set_failure(ctx, "Verilator compilation failed");
			return out;
		}
	}

	if (args_.lint_only) {
		auto t_total_end = std::chrono::steady_clock::now();
		double elapsed_s = std::chrono::duration<double>(t_total_end - t_total_start).count();
		std::string log_err;
		write_compile_log(ctx, args_.safe_output_file, combined_raw_log, log_err);
		size_t total_lines = count_log_lines(combined_raw_log);

		std::string out = std::format("# Verilog Lint Report: `{}`\n\n", target_mod);
		out += std::format("- **Status**: **PASSED** (Lint Only, {:.2f}s)\n", elapsed_s);
		out += std::format("- **Target Scope**: {}\n", scope_summary);
		out += std::format("- **Diagnostics Summary**: {} correctness warning(s), {} style/unused note(s)\n",
		                   v_diag.correctness_warnings.size(), v_diag.style_warnings.size());
		out += std::format("- **Full Log**: `{}` ({} lines, {} bytes)\n",
		                   args_.requested_output_file, total_lines, combined_raw_log.size());

		if (!v_diag.correctness_warnings.empty() || !v_diag.style_warnings.empty()) {
			out += format_categorized_warnings(v_diag, {}, {}, args_.requested_output_file);
		} else {
			out += "\n*No lint warnings or errors reported.*\n";
		}

		set_success(ctx, "Lint passed");
		return out;
	}

	// =========================================================================
	// Stage 2: Yosys Synthesis
	// =========================================================================
	std::string tmp_dir = fs_utils::get_project_tmp_dir();
	fs::path synth_json = fs::path(tmp_dir) / std::format("compile_{}_synth.json", target_mod);
	std::error_code ec_rm;
	fs::remove(synth_json, ec_rm);

	std::string synth_pass;
	if (cfg.target == "ice40") {
		synth_pass = "synth_ice40";
		if (!cfg.yosys_synth_args.empty()) {
			synth_pass += " " + cfg.yosys_synth_args;
		} else {
			synth_pass += " -abc9";
		}
		synth_pass += " -top " + target_mod + " -json " + synth_json.string() + "; check";
	} else if (cfg.target == "ecp5") {
		synth_pass = "synth_ecp5 -top " + target_mod + " -json " + synth_json.string() + "; check";
	} else {
		synth_pass = "synth -top " + target_mod + "; check; stat; write_json " + synth_json.string();
	}

	std::string yosys_cmd = fs_utils::escape_shell_arg(yosys_bin) + " -p " + fs_utils::escape_shell_arg(synth_pass);
	for (const auto &src : active_sources) {
		yosys_cmd += " " + fs_utils::escape_shell_arg(src);
	}

	std::string y_out = fs_utils::execute_command_sync(yosys_cmd);
	combined_raw_log += "================================================================================\n";
	combined_raw_log += "=== Stage 2: Yosys Synthesis (" + target_mod + ")\n";
	combined_raw_log += "=== Command: " + yosys_cmd + "\n";
	combined_raw_log += "================================================================================\n";
	combined_raw_log += y_out + "\n";

	parsed_yosys_diagnostics y_diag = parse_yosys_output(y_out, target_mod);
	bool yosys_ok = fs::exists(synth_json) && y_diag.errors.empty();

	if (!yosys_ok) {
		std::string log_err;
		write_compile_log(ctx, args_.safe_output_file, combined_raw_log, log_err);
		size_t total_lines = count_log_lines(combined_raw_log);

		std::string out = std::format("# Verilog Compile Report: `{}`\n\n", target_mod);
		out += "- **Status**: **FAILED** (Yosys Synthesis Error)\n";
		out += std::format("- **Full Log**: `{}` ({} lines, {} bytes)\n\n",
		                   args_.requested_output_file, total_lines, combined_raw_log.size());
		if (!y_diag.errors.empty()) {
			out += "## Synthesis Errors\n\n";
			for (const auto &e : y_diag.errors) {
				out += std::format("- `{}`\n", e);
			}
		}
		if (!y_diag.warnings.empty()) {
			out += std::format("\n## Synthesis Warnings ({})\n\n", y_diag.warnings.size());
			size_t shown = std::min<size_t>(15, y_diag.warnings.size());
			for (size_t i = 0; i < shown; ++i) {
				out += std::format("- `{}`\n", y_diag.warnings[i]);
			}
		}
		out += std::format("\n> **Tip**: Inspect `{}` with `fs_grep_files(path=\"{}\", query=\"ERROR|Warning\")` or `fs_read_lines`.\n",
		                   args_.requested_output_file, args_.requested_output_file);
		set_failure(ctx, "Yosys synthesis failed");
		return out;
	}

	// =========================================================================
	// Stage 3: nextpnr Place-and-Route (when compiling top module and !synth_only)
	// =========================================================================
	bool run_pnr = !args_.synth_only && is_top_module;
	bool pnr_ran = false;
	bool pnr_ok = true;
	std::string pnr_skip_guidance;
	parsed_pnr_diagnostics p_diag;
	std::map<std::string, std::pair<double, double>> domain_fmax; // domain -> (achieved_mhz, constraint_mhz)

	if (run_pnr) {
		std::vector<std::string> missing;
		if (!verilog::fpga_project_settings::is_complete(cfg, missing)) {
			pnr_skip_guidance = verilog::fpga_project_settings::format_guidance(project_root, cfg, sys_tools, pcfs);
		} else {
			std::string pnr_tool_name = (cfg.target == "ice40") ? "nextpnr-ice40" :
			                            (cfg.target == "ecp5") ? "nextpnr-ecp5" : "nextpnr-generic";
			std::string pnr_bin = fs_utils::find_executable(pnr_tool_name);
			if (pnr_bin.empty()) {
				pnr_skip_guidance = std::format("> **Note**: `{}` is not installed in PATH; skipped physical place-and-route.\n", pnr_tool_name);
			} else {
				pnr_ran = true;
				fs::path asc_file = fs::path(tmp_dir) / std::format("compile_{}_out.asc", target_mod);
				fs::path timing_json = fs::path(tmp_dir) / std::format("compile_{}_timing.json", target_mod);
				fs::remove(timing_json, ec_rm);

				std::string pnr_args = "--json " + fs_utils::escape_shell_arg(synth_json.string());
				if (cfg.target == "ice40") {
					pnr_args += " --asc " + fs_utils::escape_shell_arg(asc_file.string());
					pnr_args += " --placer heap";
					if (!cfg.device.empty()) {
						pnr_args += cfg.device.starts_with("-") ? (" " + cfg.device) : (" --" + cfg.device);
					} else {
						pnr_args += " --hx8k";
					}
					if (!cfg.package.empty()) {
						pnr_args += cfg.package.starts_with("-") ? (" " + cfg.package) : (" --package " + cfg.package);
					}
					if (!cfg.pcf_file.empty()) {
						pnr_args += " --pcf " + fs_utils::escape_shell_arg(cfg.pcf_file);
					}
					if (cfg.freq_mhz > 0.0) {
						pnr_args += std::format(" --freq {:.1f}", cfg.freq_mhz);
					}
				} else if (cfg.target == "generic") {
					pnr_args += " --uarch example";
					if (cfg.freq_mhz > 0.0) {
						pnr_args += std::format(" --freq {:.1f}", cfg.freq_mhz);
					}
				}

				std::string full_pnr_cmd = std::format("{} {} --seed 1 --report {}",
				                                       fs_utils::escape_shell_arg(pnr_bin), pnr_args,
				                                       fs_utils::escape_shell_arg(timing_json.string()));
				std::string p_out = fs_utils::execute_command_sync(full_pnr_cmd);

				combined_raw_log += "================================================================================\n";
				combined_raw_log += "=== Stage 3: nextpnr Place-and-Route (" + target_mod + ")\n";
				combined_raw_log += "=== Command: " + full_pnr_cmd + "\n";
				combined_raw_log += "================================================================================\n";
				combined_raw_log += p_out + "\n";

				p_diag = parse_nextpnr_output(p_out);
				pnr_ok = fs::exists(timing_json) && p_diag.errors.empty();

				if (fs::exists(timing_json)) {
					std::ifstream ifs(timing_json);
					if (ifs.is_open()) {
						try {
							nlohmann::json tj;
							ifs >> tj;
							if (tj.contains("fmax") && tj["fmax"].is_object()) {
								for (const auto &[dom, f_obj] : tj["fmax"].items()) {
									if (f_obj.is_object() && f_obj.contains("achieved") && f_obj["achieved"].is_number()) {
										double ach = f_obj["achieved"].get<double>();
										double con = (f_obj.contains("constraint") && f_obj["constraint"].is_number())
										             ? f_obj["constraint"].get<double>()
										             : cfg.freq_mhz;
										domain_fmax[dom] = {ach, con};
									}
								}
							}
						} catch (...) {
						}
					}
				}
			}
		}
	}

	auto t_total_end = std::chrono::steady_clock::now();
	double elapsed_s = std::chrono::duration<double>(t_total_end - t_total_start).count();

	std::string log_err;
	write_compile_log(ctx, args_.safe_output_file, combined_raw_log, log_err);
	size_t total_lines = count_log_lines(combined_raw_log);

	std::string stages_str;
	if (verilator_ran) {
		stages_str += "Verilator Lint";
	}
	if (!stages_str.empty()) {
		stages_str += " + ";
	}
	stages_str += "Yosys Synthesis";
	if (pnr_ran) {
		stages_str += " + nextpnr Place-and-Route";
	}

	std::string out = std::format("# Verilog Compile Report: `{}`\n\n", target_mod);
	out += std::format("- **Status**: **{}** ({}, {:.2f}s)\n", pnr_ok ? "PASSED" : "FAILED (nextpnr Place-and-Route)", stages_str, elapsed_s);
	out += std::format("- **Target Scope**: {}\n", scope_summary);
	out += std::format("- **Full Log**: `{}` ({} lines, {} bytes — inspect with `fs_read_lines` or `fs_grep_files`)\n",
	                   args_.requested_output_file, total_lines, combined_raw_log.size());

	// Design Health & Diagnostics
	size_t corr_warnings = v_diag.correctness_warnings.size() + y_diag.warnings.size() + p_diag.warnings.size();
	out += std::format("- **Diagnostics Summary**: {} error(s), {} correctness/synthesis warning(s), {} style/unused note(s), {} inferred latch note(s), {} combinational loop(s)\n",
	                   p_diag.errors.size(), corr_warnings, v_diag.style_warnings.size(), y_diag.latch_notes.size(), y_diag.loop_notes.size());

	if (!p_diag.errors.empty()) {
		out += "\n## Place-and-Route Errors\n\n";
		for (const auto &e : p_diag.errors) {
			out += std::format("- `{}`\n", e);
		}
	}

	if (!y_diag.loop_notes.empty() || !y_diag.latch_notes.empty()) {
		out += "\n## Critical Synthesis Hazards\n\n";
		for (const auto &loop : y_diag.loop_notes) {
			out += std::format("- **Combinational Loop**: `{}`\n", loop);
		}
		for (const auto &latch : y_diag.latch_notes) {
			out += std::format("- **Inferred Latch**: `{}`\n", latch);
		}
	}

	out += format_categorized_warnings(v_diag, y_diag.warnings, p_diag.warnings, args_.requested_output_file);

	if (!y_diag.stat_block.empty()) {
		out += std::format("\n## Synthesis Cell Statistics (`{}`)\n\n```text\n{}```\n", target_mod, y_diag.stat_block);
	}

	if (pnr_ran && (!p_diag.utilization.empty() || !domain_fmax.empty())) {
		out += "\n## Place-and-Route Utilization & Timing (Seed 1)\n\n";
		for (const auto &[dom, fpair] : domain_fmax) {
			double ach = fpair.first;
			double con = fpair.second;
			if (con > 0.0) {
				out += std::format("- **Clock `{}`**: **{:.2f} MHz** (target: {:.1f} MHz — **{}**)\n",
				                   dom, ach, con, (ach >= con) ? "PASS" : "TIMING VIOLATION");
			} else {
				out += std::format("- **Clock `{}`**: **{:.2f} MHz**\n", dom, ach);
			}
		}
		for (const auto &u : p_diag.utilization) {
			out += std::format("- {}\n", u);
		}
	}

	if (!pnr_skip_guidance.empty()) {
		out += "\n## Physical Place-and-Route Skipped\n\n";
		out += pnr_skip_guidance + "\n";
	}

	if (!pnr_ok) {
		set_failure(ctx, "nextpnr place-and-route failed");
	} else {
		set_success(ctx, "Compilation succeeded");
	}
	return out;
}

} // namespace tools
