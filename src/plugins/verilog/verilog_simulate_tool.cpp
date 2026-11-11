#include "plugins/verilog/verilog_simulate_tool.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <set>
#include <sstream>
#include <vector>
#include "fs_utils.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/vcd_compare_tool.h"
#include "plugins/verilog/vcd_parser.h"
#include "plugins/verilog/vcd_parser_cache.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include <ctre.hpp>

namespace tools
{

namespace fs = std::filesystem;

namespace
{

std::string make_tagged_vcd_path(std::string_view vcd_path, std::string_view tag)
{
	fs::path p(vcd_path);
	if (p.extension() == ".vcd") {
		fs::path tagged = p.parent_path() / std::format("{}.{}.vcd", p.stem().string(), tag);
		return tagged.generic_string();
	}
	return std::format("{}.{}.vcd", vcd_path, tag);
}

bool did_testsuite_pass(std::string_view sim_out, bool timed_out, bool has_fatal)
{
	if (timed_out || has_fatal) {
		return false;
	}

	std::string lower;
	lower.reserve(sim_out.size());
	for (unsigned char c : sim_out) {
		lower.push_back(static_cast<char>(std::tolower(c)));
	}

	// Neutralize benign zero-count summaries such as "0 failed", "0 errors", "failed: 0", "errors=0"
	std::string cleaned;
	cleaned.reserve(lower.size());
	std::string_view rem = lower;
	static constexpr auto zero_fail_re = ctre::search<
		R"(\b(?:(?:0|no|without)\s+(?:fail\w*|error\w*|mismatch\w*)|(?:fail\w*|error\w*|mismatch\w*)\s*[:=]\s*0)\b)">;
	while (auto m = zero_fail_re(rem)) {
		cleaned.append(rem.data(), static_cast<size_t>(m.data() - rem.data()));
		cleaned.push_back(' ');
		rem.remove_prefix(static_cast<size_t>((m.data() - rem.data()) + m.size()));
	}
	cleaned.append(rem);

	if (ctre::search<R"(\b(?:fail|failed|failure|failures|mismatch|mismatches|error|errors|fatal)\b)">(cleaned)) {
		return false;
	}

	return static_cast<bool>(ctre::search<R"(\b(?:pass|passed|successfully)\b)">(cleaned));
}

} // namespace

verilog_simulate_tool::verilog_simulate_tool(verilog_simulate_args args)
	: agentlib::llm_tool_action("Simulating Verilog testbench"),
	  args_(std::move(args))
{
}

bool verilog_simulate_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_simulate_tool::execute(agentlib::tool_context &ctx)
{
	if (fs_utils::find_executable("verilator").empty()) {
		set_failure(ctx, "verilator not found in PATH");
		return "Error: `verilator` was not found in system PATH. Install Verilator to run `verilog_simulate`.";
	}

	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = ".";
	}
	fs::path root_path(project_root);
	std::error_code ec;

	verilog::fpga_target_config cfg;
	std::string load_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, load_err);

	// 1. Resolve testbench file
	std::string tb_file = args_.safe_testbench;
	std::string display_tb = args_.requested_testbench;
	if (tb_file.empty()) {
		std::vector<std::string> discovered_tbs = verilog::verilog_module_scanner::find_testbench_files(root_path);
		if (discovered_tbs.empty()) {
			set_failure(ctx, "No testbench files found");
			return "Error: No testbench files found in workspace (checked `tb/`, `testbench/`, `tb_*.sv`, `*_tb.sv`). "
			       "Provide a path explicitly via `verilog_simulate(testbench=\"path/to/tb.sv\")`.";
		}
		if (discovered_tbs.size() > 1) {
			std::ostringstream msg;
			msg << "Multiple testbench files were found in the project. Specify which one to run via `verilog_simulate(testbench=\"...\")`:\n";
			for (const auto &tb : discovered_tbs) {
				fs::path rel = fs::relative(tb, root_path, ec);
				msg << std::format("- `{}`\n", (!ec && !rel.empty()) ? rel.string() : tb);
			}
			set_failure(ctx, "Multiple testbenches found; specify testbench parameter");
			return msg.str();
		}
		tb_file = discovered_tbs.front();
		fs::path rel = fs::relative(tb_file, root_path, ec);
		display_tb = (!ec && !rel.empty()) ? rel.string() : tb_file;
	}

	// 2. Discover top-level testbench module name if not specified
	std::string top_mod = args_.top_module;
	if (top_mod.empty()) {
		std::vector<std::string> single_tb = {tb_file};
		auto tb_mods = verilog::verilog_module_scanner::discover_modules_in_files(single_tb);
		if (tb_mods.empty()) {
			set_failure(ctx, "No module declaration found in testbench");
			return std::format("Error: Could not find a `module <name>` declaration in testbench `{}`.", display_tb);
		}
		top_mod = tb_mods.front().module_name;
	}

	// 3. Gather synthesizable RTL files
	fs::path scan_root = cfg.sv_path.empty()
		? ( root_path / verilog::fpga_project_settings::detect_sv_path(project_root) )
		: ( root_path / cfg.sv_path );
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
	}

	std::vector<std::string> rtl_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
	fs::path canon_tb = fs::weakly_canonical(tb_file, ec);

	std::vector<std::string> all_sources;
	all_sources.reserve(rtl_files.size() + 1);
	for (const auto &f : rtl_files) {
		fs::path canon_f = fs::weakly_canonical(f, ec);
		if (canon_f != canon_tb) {
			all_sources.push_back(f);
		}
	}
	all_sources.push_back(tb_file);

	// 4. Stage any $readmemh / $readmemb initialization files into project_root
	std::vector<std::string> staged_files = verilog::verilog_module_scanner::stage_readmem_files(all_sources, root_path);

	// 5. Determine expected VCD output path from $dumpfile("...")
	std::string dump_rel = verilog::verilog_module_scanner::extract_dumpfile_path(tb_file);
	fs::path vcd_full_path = root_path / dump_rel;

	std::string canonical_vcd;
	std::string vcd_access_err;
	if (!ctx.fs_security.validate_access(vcd_full_path.string(), agentlib::access_type::write, canonical_vcd, vcd_access_err)) {
		set_failure(ctx, vcd_access_err);
		return std::format("Error validating VCD output path `{}`: {}", dump_rel, vcd_access_err);
	}
	if (vcd_full_path.has_parent_path()) {
		fs::create_directories(vcd_full_path.parent_path(), ec);
	}
	// 6. Compile simulation binary with Verilator
	fs::path mdir = fs::absolute(fs::path(fs_utils::get_project_tmp_dir()) / std::format("verilator_{}", top_mod), ec);
	fs::create_directories(mdir, ec);
	fs::path sim_bin = mdir / std::format("V{}", top_mod);
	if (fs::exists(sim_bin, ec)) {
		fs::remove(sim_bin, ec);
	}

	std::string compile_cmd = std::format(
		"cd {} && CCACHE_DISABLE=1 OBJCACHE= verilator --binary --timing --trace --timescale 1ns/1ps -Wno-TIMESCALEMOD --Mdir {} --top-module {}",
		fs_utils::escape_shell_arg(project_root),
		fs_utils::escape_shell_arg(mdir.string()),
		fs_utils::escape_shell_arg(top_mod)
	);
	for (const auto &src : all_sources) {
		fs::path rel_src = fs::relative(src, root_path, ec);
		std::string src_arg = (!ec && !rel_src.empty()) ? rel_src.string() : fs::weakly_canonical(src, ec).string();
		compile_cmd += " " + fs_utils::escape_shell_arg(src_arg);
	}
	compile_cmd += " 2>&1";

	std::string compile_out = fs_utils::execute_command_sync(compile_cmd, 120);
	if (!fs::exists(sim_bin, ec)) {
		set_failure(ctx, "Verilator compilation failed");
		return std::format("### Verilog Simulation Failed (Compilation Error)\n\n"
		                   "- **Testbench**: `{}` (top module: `{}`)\n\n"
		                   "#### Compiler Output\n```\n{}\n```\n",
		                   display_tb, top_mod, compile_out);
	}

	// 7. Preserve any existing waveform as <stem>.previous.vcd before running the simulation binary
	std::string prev_rel = make_tagged_vcd_path(dump_rel, "previous");
	std::string lastpass_rel = make_tagged_vcd_path(dump_rel, "lastpass");
	fs::path vcd_prev_full_path = root_path / prev_rel;
	fs::path vcd_lastpass_full_path = root_path / lastpass_rel;

	bool saved_previous = false;
	if (fs::exists(vcd_full_path, ec) && fs::is_regular_file(vcd_full_path, ec)) {
		std::string canon_prev;
		std::string prev_err;
		if (ctx.fs_security.validate_access(vcd_prev_full_path.string(), agentlib::access_type::write, canon_prev, prev_err)) {
			fs::rename(vcd_full_path, vcd_prev_full_path, ec);
			if (ec) {
				ec.clear();
				fs::copy_file(vcd_full_path, vcd_prev_full_path, fs::copy_options::overwrite_existing, ec);
				if (!ec) {
					fs::remove(vcd_full_path, ec);
					ec.clear();
				}
			}
			saved_previous = !ec && fs::exists(vcd_prev_full_path, ec);
		}
	}

	// 8. Run the compiled simulation binary from project_root
	auto pre_sim_time = fs::exists(vcd_full_path, ec)
		? std::optional<fs::file_time_type>(fs::last_write_time(vcd_full_path, ec))
		: std::nullopt;

	std::string run_cmd = std::format(
		"cd {} && {} 2>&1",
		fs_utils::escape_shell_arg(project_root),
		fs_utils::escape_shell_arg(sim_bin.string())
	);
	std::string sim_out = fs_utils::execute_command_sync(run_cmd, args_.timeout_sec);

	bool timed_out = sim_out.find("Command timed out") != std::string::npos;
	bool has_fatal = sim_out.find("%Error") != std::string::npos || sim_out.find("%Fatal") != std::string::npos;
	bool suite_passed = did_testsuite_pass(sim_out, timed_out, has_fatal);

	std::ostringstream ss;
	ss << std::format("### Verilog Simulation: `{}` (`{}`)\n\n", display_tb, top_mod);
	ss << std::format("- **Status**: {}\n",
	                  timed_out ? "TIMED OUT" : (has_fatal ? "Completed with `%Error`/`%Fatal`" : "SUCCESS"));
	ss << std::format("- **RTL Files Compiled**: {} synthesizable file(s) + testbench `{}`\n",
	                  rtl_files.size(), display_tb);
	if (!staged_files.empty()) {
		std::string staged_list;
		for (size_t i = 0; i < staged_files.size(); ++i) {
			if (i > 0) {
				staged_list += ", ";
			}
			staged_list += std::format("`{}`", staged_files[i]);
		}
		ss << std::format("- **Staged `$readmem` Init Files**: {}\n", staged_list);
	}

	// Trim trailing whitespace on sim_out
	while (!sim_out.empty() && (sim_out.back() == '\n' || sim_out.back() == '\r')) {
		sim_out.pop_back();
	}
	if (!sim_out.empty()) {
		ss << "\n#### Simulation Output\n```\n" << sim_out << "\n```\n";
	}

	// 9. Parse and cache the generated VCD file, update <stem>.lastpass.vcd on PASS, and emit next-step commands
	bool vcd_exists = fs::exists(vcd_full_path, ec) && fs::is_regular_file(vcd_full_path, ec);
	bool vcd_updated = vcd_exists && (!pre_sim_time.has_value() || fs::last_write_time(vcd_full_path, ec) >= *pre_sim_time);

	bool saved_lastpass = false;
	if (vcd_exists && vcd_updated && suite_passed) {
		std::string canon_lp;
		std::string lp_err;
		if (ctx.fs_security.validate_access(vcd_lastpass_full_path.string(), agentlib::access_type::write, canon_lp, lp_err)) {
			fs::copy_file(vcd_full_path, vcd_lastpass_full_path, fs::copy_options::overwrite_existing, ec);
			saved_lastpass = !ec && fs::exists(vcd_lastpass_full_path, ec);
		}
	}
	bool lastpass_exists = fs::exists(vcd_lastpass_full_path, ec) && fs::is_regular_file(vcd_lastpass_full_path, ec);

	if (vcd_exists && vcd_updated) {
		std::string parse_err;
		auto parser_ptr = verilog::vcd_parser_cache::get_instance().get_or_parse(vcd_full_path.string(), parse_err);
		if (parser_ptr) {
			const auto &parser = *parser_ptr;
			uintmax_t fsize = fs::file_size(vcd_full_path, ec);

			ss << std::format("\n#### Generated VCD Summary (`{}`)\n", dump_rel);
			ss << std::format("- **Waveform File**: `{}` ({} bytes, loaded into LRU cache)\n", dump_rel, fsize);
			if (saved_lastpass) {
				ss << std::format("- **Passing Baseline Saved**: `{}`\n", lastpass_rel);
			}
			ss << std::format("- **Timescale & Duration**: `{}` | `{} to {} {}` (Duration: `{} {}`)\n",
			                  parser.get_timescale(),
			                  parser.get_start_time(),
			                  parser.get_end_time(),
			                  parser.get_timescale(),
			                  parser.get_end_time() - parser.get_start_time(),
			                  parser.get_timescale());

			auto clk_info = parser.detect_clock();
			std::string clk_short_name;
			if (clk_info && clk_info->period > 0) {
				ss << std::format("- **Detected Clock**: `{}` ({} posedge cycles, period: {} {})\n",
				                  clk_info->signal_name,
				                  clk_info->posedge_count,
				                  clk_info->period,
				                  parser.get_timescale());
				size_t dot = clk_info->signal_name.rfind('.');
				clk_short_name = (dot != std::string::npos) ? clk_info->signal_name.substr(dot + 1) : clk_info->signal_name;
			}

			// Gather top scopes and sample signals for actionable next-step suggestions
			const auto &signals = parser.get_signals();
			std::set<std::string> scopes;
			std::string suggested_scope = top_mod;
			for (const auto &sig : signals) {
				if (!sig.scope.empty()) {
					scopes.insert(sig.scope);
					// Prefer the immediate DUT instance under top_mod (e.g. "tb_main.uut")
					if (sig.scope.starts_with(top_mod + ".") &&
					    sig.scope.find('.', top_mod.size() + 1) == std::string::npos) {
						suggested_scope = sig.scope;
					}
				}
			}

			std::vector<std::string> sample_sigs;
			std::set<std::string> seen_short;
			for (const auto &sig : signals) {
				if (sig.scope == top_mod || sig.scope == suggested_scope) {
					if (sig.short_name == clk_short_name) {
						continue;
					}
					if (seen_short.insert(sig.short_name).second) {
						sample_sigs.push_back(sig.short_name);
						if (sample_sigs.size() >= 3) {
							break;
						}
					}
				}
			}
			if (sample_sigs.empty() && !signals.empty()) {
				sample_sigs.push_back(signals.front().short_name);
			}

			std::string sigs_arg = "[";
			for (size_t i = 0; i < sample_sigs.size(); ++i) {
				if (i > 0) {
					sigs_arg += ", ";
				}
				sigs_arg += std::format("\"{}\"", sample_sigs[i]);
			}
			sigs_arg += "]";

			ss << std::format("- **Total Signals**: {} across {} hierarchy scope(s)\n",
			                  signals.size(), scopes.size());

			vcd_compare_tool::quick_compare_summary prev_cmp;
			if (saved_previous) {
				prev_cmp = vcd_compare_tool::quick_compare(vcd_prev_full_path.string(), vcd_full_path.string(), suggested_scope);
				if (prev_cmp.ok) {
					if (prev_cmp.identical) {
						ss << std::format("- **Comparison vs. Previous (`{}`)**: **IDENTICAL** (0 differences across {} signal(s) and {} clock cycle(s))\n",
						                  prev_rel, prev_cmp.common_signals, prev_cmp.cycles_b);
					} else if (prev_cmp.divergent_cycles > 0) {
						ss << std::format("- **Comparison vs. Previous (`{}`)**: **DIVERGED** ({} signal(s) differ across {} of {} cycle(s); first difference at tick {})\n",
						                  prev_rel, prev_cmp.divergent_signals, prev_cmp.divergent_cycles,
						                  std::min(prev_cmp.cycles_a, prev_cmp.cycles_b), prev_cmp.first_divergent_tick);
					} else {
						ss << std::format("- **Comparison vs. Previous (`{}`)**: **LENGTH MISMATCH** ({} vs {} cycles)\n",
						                  prev_rel, prev_cmp.cycles_a, prev_cmp.cycles_b);
					}
				}
			}

			ss << "\n#### Next Steps\n";
			ss << std::format("- Use `vcd_file_info(path=\"{}\", scope=\"{}\")` to get more information.\n",
			                  dump_rel, suggested_scope);
			ss << std::format("- Use `vcd_wave(path=\"{}\", signals={})` to see the waveforms.\n",
			                  dump_rel, sigs_arg);
			if (saved_previous) {
				if (prev_cmp.ok && prev_cmp.identical) {
					ss << std::format("- Previous result saved as `{}`: waveforms are **IDENTICAL** across all {} signal(s) ({} cycles) — no `vcd_compare` needed.\n",
					                  prev_rel, prev_cmp.common_signals, prev_cmp.cycles_b);
				} else if (prev_cmp.ok && prev_cmp.divergent_cycles > 0) {
					ss << std::format("- Previous result saved as `{}` (diverged at tick {}, {} signal(s) differ). To compare the current result to this file use `vcd_compare(path_a=\"{}\", path_b=\"{}\", scope=\"{}\")`.\n",
					                  prev_rel, prev_cmp.first_divergent_tick, prev_cmp.divergent_signals, prev_rel, dump_rel, suggested_scope);
				} else {
					ss << std::format("- Previous result saved as `{}`. To compare the current result to this file use `vcd_compare(path_a=\"{}\", path_b=\"{}\", scope=\"{}\")`.\n",
					                  prev_rel, prev_rel, dump_rel, suggested_scope);
				}
			}
			if (!suite_passed && lastpass_exists) {
				auto lp_cmp = vcd_compare_tool::quick_compare(vcd_lastpass_full_path.string(), vcd_full_path.string(), suggested_scope);
				if (lp_cmp.ok && lp_cmp.identical) {
					ss << std::format("- Last passing run (`{}`) waveform is **IDENTICAL** to the current result ({} signal(s), {} cycles).\n",
					                  lastpass_rel, lp_cmp.common_signals, lp_cmp.cycles_b);
				} else if (lp_cmp.ok && lp_cmp.divergent_cycles > 0) {
					ss << std::format("- Compared to last passing run (`{}`): diverged at tick {} ({} signal(s) differ). To compare to the last passing run, use `vcd_compare(path_a=\"{}\", path_b=\"{}\", scope=\"{}\")`.\n",
					                  lastpass_rel, lp_cmp.first_divergent_tick, lp_cmp.divergent_signals, lastpass_rel, dump_rel, suggested_scope);
				} else {
					ss << std::format("- To compare to the last passing run, use `vcd_compare(path_a=\"{}\", path_b=\"{}\", scope=\"{}\")`.\n",
					                  lastpass_rel, dump_rel, suggested_scope);
				}
			}
		} else {
			ss << std::format("\n> [!WARNING] VCD file `{}` was generated but could not be parsed: {}\n",
			                  dump_rel, parse_err);
		}
	} else {
		ss << std::format("\n> [!NOTE] No VCD file was produced at `{}`. Ensure the testbench calls `$dumpfile(\"{}\"); $dumpvars(0, {});`.\n",
		                  dump_rel, dump_rel, top_mod);
	}

	if (timed_out) {
		set_failure(ctx, "Simulation timed out");
	} else {
		set_success(ctx);
	}
	return ss.str();
}

} // namespace tools
