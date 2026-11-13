#include "plugins/verilog/verilog_simulate_tool.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
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

std::string make_collision_renamed_vcd_path(std::string_view raw_dump_rel, const fs::path &tb_path)
{
	fs::path raw_p(raw_dump_rel);
	std::string tb_stem = tb_path.stem().string();
	if (tb_stem.empty()) {
		tb_stem = "tb";
	}
	fs::path renamed = raw_p.parent_path() / std::format("{}.vcd", tb_stem);
	return renamed.generic_string();
}

std::string suggest_unique_dumpfile_path(std::string_view raw_dump_rel, const fs::path &tb_path)
{
	fs::path raw_p(raw_dump_rel);
	std::string stem = tb_path.stem().string();
	if (stem.starts_with("tb_") && stem.size() > 3) {
		stem = stem.substr(3);
	} else if (stem.ends_with("_tb") && stem.size() > 3) {
		stem = stem.substr(0, stem.size() - 3);
	}
	fs::path suggested = raw_p.parent_path() / std::format("dump_{}.vcd", stem.empty() ? "tb" : stem);
	return suggested.generic_string();
}

enum class testsuite_verdict {
	passed,
	completed_no_assertions,
	failed
};

testsuite_verdict classify_testsuite_output(std::string_view sim_out, bool timed_out, bool has_fatal)
{
	if (timed_out || has_fatal) {
		return testsuite_verdict::failed;
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
		return testsuite_verdict::failed;
	}

	if (ctre::search<R"(\b(?:pass|passed|successfully)\b)">(cleaned)) {
		return testsuite_verdict::passed;
	}

	return testsuite_verdict::completed_no_assertions;
}

struct tb_run_plan {
	std::string tb_file;
	std::string display_tb;
	std::string level{"quick"};
	std::vector<std::string> matched_modules;
	std::string raw_dump_rel;
	std::string effective_dump_rel;
	bool has_collision{false};
	std::vector<std::string> colliding_tbs;
};

struct tb_run_outcome {
	std::string display_tb;
	std::string top_mod;
	std::string level;
	std::vector<std::string> matched_modules;
	std::string effective_dump_rel;
	bool has_collision{false};
	bool compile_failed{false};
	bool timed_out{false};
	bool has_fatal{false};
	bool suite_passed{false};
	bool suite_failed{false};
	bool completed_no_assertions{false};
	std::string compare_status;
	std::string section_markdown;
};

struct tb_compile_result {
	std::string top_mod;
	std::vector<std::string> all_sources;
	fs::path mdir;
	fs::path sim_bin;
	bool compile_failed{false};
	std::string error_markdown;
};

tb_compile_result compile_single_testbench(
	const fs::path &root_path,
	const std::string &project_root,
	const std::vector<std::string> &rtl_files,
	const tb_run_plan &plan,
	const std::string &override_top_mod,
	size_t plan_idx,
	size_t build_jobs)
{
	tb_compile_result res;
	std::error_code ec;

	// 1. Discover top-level testbench module name if not specified
	std::string top_mod = override_top_mod;
	if (top_mod.empty()) {
		std::vector<std::string> single_tb = {plan.tb_file};
		auto tb_mods = verilog::verilog_module_scanner::discover_modules_in_files(single_tb);
		if (tb_mods.empty()) {
			res.compile_failed = true;
			res.error_markdown = std::format("Error: Could not find a `module <name>` declaration in testbench `{}`.", plan.display_tb);
			return res;
		}
		top_mod = tb_mods.front().module_name;
	}
	res.top_mod = top_mod;

	// 2. Gather synthesizable RTL files + this testbench
	fs::path canon_tb = fs::weakly_canonical(plan.tb_file, ec);
	res.all_sources.reserve(rtl_files.size() + 1);
	for (const auto &f : rtl_files) {
		fs::path canon_f = fs::weakly_canonical(f, ec);
		if (canon_f != canon_tb) {
			res.all_sources.push_back(f);
		}
	}
	res.all_sources.push_back(plan.tb_file);

	// 3. Build an isolated --Mdir per testbench so parallel Verilator compiles never collide,
	//    even if two testbench files declare the same module name (e.g. `module tb;`).
	std::string tb_stem = fs::path(plan.display_tb).stem().string();
	std::string safe_stem;
	safe_stem.reserve(tb_stem.size());
	for (unsigned char c : tb_stem) {
		safe_stem.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
	}
	std::string mdir_leaf;
	if (safe_stem.empty() || safe_stem == top_mod) {
		mdir_leaf = std::format("verilator_{}", top_mod);
	} else {
		mdir_leaf = std::format("verilator_{}_{}", safe_stem, top_mod);
	}
	if (plan_idx > 0 && safe_stem.empty()) {
		mdir_leaf += std::format("_{}", plan_idx);
	}

	fs::path base_tmp = (fs::weakly_canonical(root_path, ec) == fs::weakly_canonical(fs::current_path(ec), ec))
		? fs::path(fs_utils::get_project_tmp_dir())
		: (root_path / ".turbostar_tmp");
	res.mdir = fs::absolute(base_tmp / mdir_leaf, ec);
	fs::create_directories(res.mdir, ec);
	res.sim_bin = res.mdir / std::format("V{}", top_mod);
	if (fs::exists(res.sim_bin, ec)) {
		fs::remove(res.sim_bin, ec);
	}

	std::string compile_cmd = std::format(
		"cd {} && CCACHE_DISABLE=1 OBJCACHE= verilator --binary --build-jobs {} --timing --trace --timescale 1ns/1ps -Wno-TIMESCALEMOD --Mdir {} --top-module {}",
		fs_utils::escape_shell_arg(project_root),
		std::max<size_t>(1, build_jobs),
		fs_utils::escape_shell_arg(res.mdir.string()),
		fs_utils::escape_shell_arg(top_mod)
	);
	for (const auto &src : res.all_sources) {
		fs::path rel_src = fs::relative(src, root_path, ec);
		std::string src_arg = (!ec && !rel_src.empty()) ? rel_src.string() : fs::weakly_canonical(src, ec).string();
		compile_cmd += " " + fs_utils::escape_shell_arg(src_arg);
	}
	compile_cmd += " 2>&1";

	std::string compile_out = fs_utils::execute_command_sync(compile_cmd, 120);
	if (!fs::exists(res.sim_bin, ec)) {
		res.compile_failed = true;
		res.error_markdown = std::format("### Verilog Simulation Failed (Compilation Error)\n\n"
		                                 "- **Testbench**: `{}` (top module: `{}`)\n\n"
		                                 "#### Compiler Output\n```\n{}\n```\n",
		                                 plan.display_tb, top_mod, compile_out);
	}
	return res;
}

tb_run_outcome run_compiled_testbench(
	agentlib::tool_context &ctx,
	const fs::path &root_path,
	const std::string &project_root,
	const std::vector<std::string> &rtl_files,
	const tb_run_plan &plan,
	const tb_compile_result &compiled,
	int timeout_sec)
{
	tb_run_outcome out;
	out.display_tb = plan.display_tb;
	out.top_mod = compiled.top_mod;
	out.level = plan.level;
	out.matched_modules = plan.matched_modules;
	out.effective_dump_rel = plan.effective_dump_rel;
	out.has_collision = plan.has_collision;

	if (compiled.compile_failed) {
		out.compile_failed = true;
		out.suite_failed = true;
		out.section_markdown = compiled.error_markdown;
		return out;
	}

	const std::string &top_mod = compiled.top_mod;
	const std::vector<std::string> &all_sources = compiled.all_sources;
	const fs::path &sim_bin = compiled.sim_bin;
	std::error_code ec;

	// 3. Stage any $readmemh / $readmemb initialization files into project_root
	std::vector<std::string> staged_files = verilog::verilog_module_scanner::stage_readmem_files(all_sources, root_path);

	// 4. Validate VCD output path(s) if the testbench declares a VCD dump
	bool expects_vcd = !plan.effective_dump_rel.empty();
	fs::path raw_vcd_full_path = expects_vcd ? (root_path / plan.raw_dump_rel) : fs::path();
	fs::path vcd_full_path = expects_vcd ? (root_path / plan.effective_dump_rel) : fs::path();

	if (expects_vcd) {
		std::string canonical_vcd;
		std::string vcd_access_err;
		if (!ctx.fs_security.validate_access(vcd_full_path.string(), agentlib::access_type::write, canonical_vcd, vcd_access_err)) {
			out.compile_failed = true;
			out.suite_failed = true;
			out.section_markdown = std::format("Error validating VCD output path `{}`: {}", plan.effective_dump_rel, vcd_access_err);
			return out;
		}
		if (vcd_full_path.has_parent_path()) {
			fs::create_directories(vcd_full_path.parent_path(), ec);
		}
		if (raw_vcd_full_path.has_parent_path()) {
			fs::create_directories(raw_vcd_full_path.parent_path(), ec);
		}
	}

	// 6. Preserve any existing waveform at effective_dump_rel as <stem>.previous.vcd before running
	const std::string &dump_rel = plan.effective_dump_rel;
	std::string prev_rel = expects_vcd ? make_tagged_vcd_path(dump_rel, "previous") : "";
	std::string lastpass_rel = expects_vcd ? make_tagged_vcd_path(dump_rel, "lastpass") : "";
	fs::path vcd_prev_full_path = expects_vcd ? (root_path / prev_rel) : fs::path();
	fs::path vcd_lastpass_full_path = expects_vcd ? (root_path / lastpass_rel) : fs::path();

	bool saved_previous = false;
	if (expects_vcd && fs::exists(vcd_full_path, ec) && fs::is_regular_file(vcd_full_path, ec)) {
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

	// If raw_vcd_full_path != vcd_full_path (due to collision renaming) and raw_vcd_full_path exists on disk,
	// stash it temporarily so the simulation binary produces a clean raw file without clobbering unrelated files.
	fs::path raw_stash_path;
	bool stashed_raw = false;
	if (expects_vcd && plan.has_collision && raw_vcd_full_path != vcd_full_path && fs::exists(raw_vcd_full_path, ec)) {
		raw_stash_path = raw_vcd_full_path.string() + ".stash_tmp";
		fs::rename(raw_vcd_full_path, raw_stash_path, ec);
		stashed_raw = !ec;
		ec.clear();
	}

	// 7. Run the compiled simulation binary from project_root
	auto pre_sim_time = (expects_vcd && fs::exists(raw_vcd_full_path, ec))
		? std::optional<fs::file_time_type>(fs::last_write_time(raw_vcd_full_path, ec))
		: std::nullopt;

	std::string run_cmd = std::format(
		"cd {} && {} 2>&1",
		fs_utils::escape_shell_arg(project_root),
		fs_utils::escape_shell_arg(sim_bin.string())
	);
	std::string sim_out = fs_utils::execute_command_sync(run_cmd, timeout_sec);

	// If collision renaming is active, move newly generated raw_vcd_full_path -> vcd_full_path
	bool raw_exists = expects_vcd && fs::exists(raw_vcd_full_path, ec) && fs::is_regular_file(raw_vcd_full_path, ec);
	bool raw_updated = raw_exists && (!pre_sim_time.has_value() || fs::last_write_time(raw_vcd_full_path, ec) >= *pre_sim_time);
	if (expects_vcd && plan.has_collision && raw_vcd_full_path != vcd_full_path && raw_updated) {
		fs::rename(raw_vcd_full_path, vcd_full_path, ec);
		if (ec) {
			ec.clear();
			fs::copy_file(raw_vcd_full_path, vcd_full_path, fs::copy_options::overwrite_existing, ec);
			fs::remove(raw_vcd_full_path, ec);
			ec.clear();
		}
	}
	if (stashed_raw && fs::exists(raw_stash_path, ec)) {
		fs::rename(raw_stash_path, raw_vcd_full_path, ec);
		ec.clear();
	}

	out.timed_out = sim_out.find("Command timed out") != std::string::npos;
	out.has_fatal = sim_out.find("%Error") != std::string::npos || sim_out.find("%Fatal") != std::string::npos;
	auto verdict = classify_testsuite_output(sim_out, out.timed_out, out.has_fatal);
	out.suite_passed = (verdict == testsuite_verdict::passed);
	out.suite_failed = (verdict == testsuite_verdict::failed);
	out.completed_no_assertions = (verdict == testsuite_verdict::completed_no_assertions);

	std::string status_banner;
	if (out.timed_out) {
		status_banner = "TIMED OUT";
	} else if (out.has_fatal) {
		status_banner = "Completed with `%Error`/`%Fatal`";
	} else if (out.suite_failed) {
		status_banner = "FAIL (Assertion/Check Failed)";
	} else if (out.suite_passed) {
		status_banner = "PASS (SUCCESS)";
	} else {
		status_banner = "INFO (Completed cleanly, no PASS/FAIL assertions)";
	}

	std::ostringstream ss;
	ss << std::format("### Verilog Simulation: `{}` (`{}`)\n\n", plan.display_tb, top_mod);
	ss << std::format("- **Status**: {}\n", status_banner);
	if (!plan.matched_modules.empty()) {
		std::string mods_str;
		for (size_t i = 0; i < plan.matched_modules.size(); ++i) {
			if (i > 0) {
				mods_str += ", ";
			}
			mods_str += std::format("`{}`", plan.matched_modules[i]);
		}
		ss << std::format("- **Tested Module(s)**: {} (level: `{}`)\n", mods_str, plan.level);
	}
	ss << std::format("- **RTL Files Compiled**: {} synthesizable file(s) + testbench `{}`\n",
	                  rtl_files.size(), plan.display_tb);
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

	if (plan.has_collision) {
		std::string others;
		for (const auto &ctb : plan.colliding_tbs) {
			if (ctb == plan.display_tb) {
				continue;
			}
			if (!others.empty()) {
				others += ", ";
			}
			others += std::format("`{}`", ctb);
		}
		std::string suggested_vcd = suggest_unique_dumpfile_path(plan.raw_dump_rel, plan.display_tb);
		ss << std::format("\n> [!WARNING] **Waveform `$dumpfile` Collision**: Testbench `{}` writes to `{}`{}",
		                  plan.display_tb, plan.raw_dump_rel,
		                  others.empty() ? "" : std::format(" (collides with {})", others));
		ss << std::format(". Renamed generated waveform to `{}`.\n"
		                  "> **Recommended Fix**: Edit `{}` and rename `$dumpfile(\"{}\")` -> `$dumpfile(\"{}\")` so each testbench writes to a unique waveform file.\n",
		                  dump_rel, plan.display_tb, plan.raw_dump_rel, suggested_vcd);
	}

	while (!sim_out.empty() && (sim_out.back() == '\n' || sim_out.back() == '\r')) {
		sim_out.pop_back();
	}
	if (!sim_out.empty()) {
		ss << "\n#### Simulation Output\n```\n" << sim_out << "\n```\n";
	}

	// 8. Parse and cache the generated VCD file, update <stem>.lastpass.vcd on PASS, and emit next-step commands
	bool vcd_exists = expects_vcd && fs::exists(vcd_full_path, ec) && fs::is_regular_file(vcd_full_path, ec);
	bool vcd_updated = vcd_exists && (plan.has_collision ? raw_updated : (!pre_sim_time.has_value() || fs::last_write_time(vcd_full_path, ec) >= *pre_sim_time));

	bool saved_lastpass = false;
	if (vcd_exists && vcd_updated && out.suite_passed) {
		std::string canon_lp;
		std::string lp_err;
		if (ctx.fs_security.validate_access(vcd_lastpass_full_path.string(), agentlib::access_type::write, canon_lp, lp_err)) {
			fs::copy_file(vcd_full_path, vcd_lastpass_full_path, fs::copy_options::overwrite_existing, ec);
			saved_lastpass = !ec && fs::exists(vcd_lastpass_full_path, ec);
		}
	}
	bool lastpass_exists = expects_vcd && fs::exists(vcd_lastpass_full_path, ec) && fs::is_regular_file(vcd_lastpass_full_path, ec);

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

			const auto &signals = parser.get_signals();
			std::set<std::string> scopes;
			std::string suggested_scope = top_mod;
			for (const auto &sig : signals) {
				if (!sig.scope.empty()) {
					scopes.insert(sig.scope);
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
						out.compare_status = "IDENTICAL to previous";
						ss << std::format("- **Comparison vs. Previous (`{}`)**: **IDENTICAL** (0 differences across {} signal(s) and {} clock cycle(s))\n",
						                  prev_rel, prev_cmp.common_signals, prev_cmp.cycles_b);
					} else if (prev_cmp.divergent_cycles > 0) {
						out.compare_status = std::format("DIVERGED at tick {} ({} sigs)", prev_cmp.first_divergent_tick, prev_cmp.divergent_signals);
						ss << std::format("- **Comparison vs. Previous (`{}`)**: **DIVERGED** ({} signal(s) differ across {} of {} cycle(s); first difference at tick {})\n",
						                  prev_rel, prev_cmp.divergent_signals, prev_cmp.divergent_cycles,
						                  std::min(prev_cmp.cycles_a, prev_cmp.cycles_b), prev_cmp.first_divergent_tick);
					} else {
						out.compare_status = std::format("LENGTH MISMATCH ({} vs {} cycles)", prev_cmp.cycles_a, prev_cmp.cycles_b);
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
			if (!out.suite_passed && lastpass_exists) {
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
	} else if (expects_vcd) {
		ss << std::format("\n> [!NOTE] No VCD file was produced at `{}`. Ensure the testbench calls `$dumpfile(\"{}\"); $dumpvars(0, {});`.\n",
		                  dump_rel, plan.raw_dump_rel, top_mod);
	} else {
		ss << std::format("\n> [!NOTE] No VCD file was produced (testbench does not call `$dumpfile`/`$dumpvars`). To capture waveforms, add `$dumpfile(\"dump_{}.vcd\"); $dumpvars(0, {});`.\n",
		                  top_mod, top_mod);
	}

	out.section_markdown = ss.str();
	return out;
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

	fs::path scan_root = cfg.sv_path.empty()
		? (root_path / verilog::fpga_project_settings::detect_sv_path(project_root))
		: (root_path / cfg.sv_path);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
	}
	std::vector<std::string> rtl_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
	std::vector<std::string> discovered_tbs = verilog::verilog_module_scanner::find_testbench_files(root_path);

	auto to_rel_tb = [&](std::string_view p_str) -> std::string {
		fs::path p(p_str);
		if (!p.is_absolute()) {
			p = root_path / p;
		}
		fs::path canon = fs::weakly_canonical(p, ec);
		fs::path canon_root = fs::weakly_canonical(root_path, ec);
		fs::path rel = fs::relative(canon, canon_root, ec);
		if (!ec && !rel.empty() && !rel.generic_string().starts_with("..")) {
			return rel.generic_string();
		}
		return fs::path(p_str).generic_string();
	};

	// Load configured testbench entries from turbostar.json / project.json
	std::vector<verilog::testbench_entry> configured_entries;
	std::string tb_cfg_path;
	std::string tb_cfg_err;
	verilog::fpga_project_settings::load_testbenches(project_root, configured_entries, tb_cfg_path, tb_cfg_err);

	// Build unified catalog of testbenches (configured + auto-discovered on disk) and detect $dumpfile collisions
	struct catalog_tb {
		std::string full_path;
		std::string rel_path;
		std::vector<std::string> modules;
		std::string level{"quick"};
		std::string raw_dump_rel;
		bool explicitly_configured{false};
	};
	std::vector<catalog_tb> catalog;
	std::map<std::string, size_t> rel_to_idx;

	for (const auto &ce : configured_entries) {
		std::string rel = to_rel_tb(ce.testbench);
		fs::path full = root_path / rel;
		if (!fs::exists(full, ec) || !fs::is_regular_file(full, ec)) {
			continue;
		}
		catalog_tb item;
		item.full_path = full.string();
		item.rel_path = rel;
		item.modules = ce.modules;
		item.level = ce.level.empty() ? "quick" : ce.level;
		item.raw_dump_rel = verilog::verilog_module_scanner::extract_dumpfile_path(full);
		item.explicitly_configured = true;
		rel_to_idx[rel] = catalog.size();
		catalog.push_back(std::move(item));
	}

	for (const auto &dtb : discovered_tbs) {
		std::string rel = to_rel_tb(dtb);
		if (rel_to_idx.contains(rel)) {
			continue;
		}
		catalog_tb item;
		item.full_path = dtb;
		item.rel_path = rel;
		item.modules = verilog::verilog_module_scanner::discover_instantiated_modules(dtb, rtl_files);
		item.level = "quick";
		item.raw_dump_rel = verilog::verilog_module_scanner::extract_dumpfile_path(dtb);
		item.explicitly_configured = false;
		rel_to_idx[rel] = catalog.size();
		catalog.push_back(std::move(item));
	}

	std::map<std::string, std::vector<std::string>> dumpfile_to_tbs;
	for (const auto &item : catalog) {
		if (!item.raw_dump_rel.empty()) {
			dumpfile_to_tbs[item.raw_dump_rel].push_back(item.rel_path);
		}
	}

	auto make_plan_from_catalog = [&](const catalog_tb &item, std::vector<std::string> matched_mods) -> tb_run_plan {
		tb_run_plan plan;
		plan.tb_file = item.full_path;
		plan.display_tb = item.rel_path;
		plan.level = item.level;
		plan.matched_modules = std::move(matched_mods);
		plan.raw_dump_rel = item.raw_dump_rel;
		if (!item.raw_dump_rel.empty()) {
			const auto &colliding = dumpfile_to_tbs[item.raw_dump_rel];
			if (colliding.size() > 1) {
				plan.has_collision = true;
				plan.colliding_tbs = colliding;
				plan.effective_dump_rel = make_collision_renamed_vcd_path(item.raw_dump_rel, item.rel_path);
			} else {
				plan.has_collision = false;
				plan.effective_dump_rel = item.raw_dump_rel;
			}
		}
		return plan;
	};

	std::vector<tb_run_plan> plans;
	std::string override_top_mod = args_.top_module;
	std::vector<std::string> target_hierarchy;
	bool effective_hierarchical = args_.hierarchical;
	bool hier_from_project_settings = false;

	// Case A: Explicit testbench file passed
	if (!args_.safe_testbench.empty()) {
		// Backward compatibility: if both testbench and module were passed and top_module was empty,
		// check if requested_module is the top module inside safe_testbench
		if (override_top_mod.empty() && !args_.requested_module.empty()) {
			std::vector<std::string> single_tb = {args_.safe_testbench};
			auto tb_mods = verilog::verilog_module_scanner::discover_modules_in_files(single_tb);
			for (const auto &m : tb_mods) {
				if (m.module_name == args_.requested_module) {
					override_top_mod = args_.requested_module;
					break;
				}
			}
		}

		std::string rel = to_rel_tb(args_.safe_testbench);
		auto it = rel_to_idx.find(rel);
		if (it != rel_to_idx.end()) {
			plans.push_back(make_plan_from_catalog(catalog[it->second], catalog[it->second].modules));
		} else {
			tb_run_plan plan;
			plan.tb_file = args_.safe_testbench;
			plan.display_tb = args_.requested_testbench.empty() ? rel : args_.requested_testbench;
			plan.raw_dump_rel = verilog::verilog_module_scanner::extract_dumpfile_path(args_.safe_testbench);
			if (!plan.raw_dump_rel.empty()) {
				const auto &colliding = dumpfile_to_tbs[plan.raw_dump_rel];
				if (colliding.size() > 1) {
					plan.has_collision = true;
					plan.colliding_tbs = colliding;
					plan.effective_dump_rel = make_collision_renamed_vcd_path(plan.raw_dump_rel, plan.display_tb);
				} else {
					plan.effective_dump_rel = plan.raw_dump_rel;
				}
			}
			plans.push_back(std::move(plan));
		}
	}
	// Case B: Target RTL module specified (`module="..."`)
	else if (!args_.requested_module.empty()) {
		std::string resolved_mod;
		std::string safe_mod_file;
		std::string dummy_err;
		if (ctx.fs_security.validate_access(args_.requested_module, agentlib::access_type::read, safe_mod_file, dummy_err) &&
		    fs_utils::is_regular_file(safe_mod_file)) {
			resolved_mod = verilog::verilog_module_scanner::resolve_module_name("", safe_mod_file, rtl_files);
		}
		if (resolved_mod.empty()) {
			resolved_mod = verilog::verilog_module_scanner::resolve_module_name(args_.requested_module, "", rtl_files);
		}

		if (!args_.hierarchical_explicit) {
			std::vector<std::string> hier_modules;
			std::string hier_err;
			verilog::fpga_project_settings::load_hierarchical_modules(project_root, hier_modules, hier_err);
			if (std::find(hier_modules.begin(), hier_modules.end(), resolved_mod) != hier_modules.end() ||
			    std::find(hier_modules.begin(), hier_modules.end(), args_.requested_module) != hier_modules.end()) {
				effective_hierarchical = true;
				hier_from_project_settings = true;
			}
		}

		if (effective_hierarchical) {
			target_hierarchy = verilog::verilog_module_scanner::discover_submodule_hierarchy(resolved_mod, rtl_files);
		} else {
			target_hierarchy = {resolved_mod};
		}

		int max_rank = verilog::fpga_project_settings::test_level_rank(args_.level);
		std::set<std::string> scheduled_rels;
		std::vector<std::string> skipped_by_level;

		for (const auto &mod_name : target_hierarchy) {
			for (const auto &item : catalog) {
				if (std::find(item.modules.begin(), item.modules.end(), mod_name) == item.modules.end()) {
					continue;
				}
				if (verilog::fpga_project_settings::test_level_rank(item.level) > max_rank) {
					if (std::find(skipped_by_level.begin(), skipped_by_level.end(), item.rel_path) == skipped_by_level.end()) {
						skipped_by_level.push_back(std::format("`{}` (level: `{}`)", item.rel_path, item.level));
					}
					continue;
				}
				if (scheduled_rels.insert(item.rel_path).second) {
					// Collect all modules in target_hierarchy tested by this testbench
					std::vector<std::string> matched;
					for (const auto &tm : target_hierarchy) {
						if (std::find(item.modules.begin(), item.modules.end(), tm) != item.modules.end()) {
							matched.push_back(tm);
						}
					}
					plans.push_back(make_plan_from_catalog(item, std::move(matched)));
				}
			}
		}

		// Fallback: check if requested_module matches the testbench module name itself (e.g. "tb_main")
		if (plans.empty() && skipped_by_level.empty()) {
			for (const auto &item : catalog) {
				std::vector<std::string> single_tb = {item.full_path};
				auto tb_mods = verilog::verilog_module_scanner::discover_modules_in_files(single_tb);
				for (const auto &tm : tb_mods) {
					if (tm.module_name == args_.requested_module) {
						override_top_mod = args_.requested_module;
						plans.push_back(make_plan_from_catalog(item, item.modules));
						break;
					}
				}
				if (!plans.empty()) {
					break;
				}
			}
		}

		if (plans.empty()) {
			std::ostringstream msg;
			if (!skipped_by_level.empty()) {
				std::string skipped_str;
				for (size_t i = 0; i < skipped_by_level.size(); ++i) {
					if (i > 0) skipped_str += ", ";
					skipped_str += skipped_by_level[i];
				}
				msg << std::format("No testbenches for module `{}` matched `level=\"{}\"`. Skipped higher-tier testbench(es): {}.\n"
				                   "Re-run with `verilog_simulate(module=\"{}\", level=\"extreme\")` to include them.",
				                   resolved_mod, args_.level, skipped_str, args_.requested_module);
			} else {
				// If effective_hierarchical=false, check whether submodules of resolved_mod have testbenches!
				std::vector<std::string> sub_hierarchy;
				std::set<std::string> sub_tbs;
				std::vector<std::string> tested_submods;
				if (!effective_hierarchical) {
					sub_hierarchy = verilog::verilog_module_scanner::discover_submodule_hierarchy(resolved_mod, rtl_files);
					for (const auto &sm : sub_hierarchy) {
						if (sm == resolved_mod) {
							continue;
						}
						bool sm_has_tb = false;
						for (const auto &item : catalog) {
							if (std::find(item.modules.begin(), item.modules.end(), sm) != item.modules.end()) {
								sub_tbs.insert(item.rel_path);
								sm_has_tb = true;
							}
						}
						if (sm_has_tb) {
							tested_submods.push_back(sm);
						}
					}
				}

				if (!sub_tbs.empty()) {
					std::string submods_str;
					for (size_t i = 0; i < tested_submods.size(); ++i) {
						if (i > 0) submods_str += ", ";
						submods_str += std::format("`{}`", tested_submods[i]);
					}
					msg << std::format("No direct testbench is associated with module `{}`, but **{} testbench(es)** exist for its submodules ({}).\n"
					                   "- **Recommended (One-Time Project Config)**: Run `verilog_configure_testbench(module=\"{}\", hierarchical=true)` so `verilog_simulate(module=\"{}\")` automatically runs its submodule testbenches by default.\n"
					                   "- **One-Off Run**: Run `verilog_simulate(module=\"{}\", hierarchical=true)` to verify `{}` bottom-up via its submodule testbenches.\n"
					                   "- **Or Add Direct Testbench**: Associate a direct testbench using `verilog_configure_testbench(testbench=\"path/to/tb.sv\", modules=[\"{}\"])`.",
					                   resolved_mod, sub_tbs.size(), submods_str,
					                   resolved_mod, args_.requested_module,
					                   args_.requested_module, resolved_mod, resolved_mod);
				} else {
					msg << std::format("No testbenches are associated with module `{}`", resolved_mod);
					if (effective_hierarchical && target_hierarchy.size() > 1) {
						msg << " (or its submodules)";
					}
					msg << ".\nAssociate a testbench using `verilog_configure_testbench(testbench=\"path/to/tb.sv\", modules=[\""
					    << resolved_mod << "\"])` or pass `testbench=\"path/to/tb.sv\"` directly.";
				}
			}
			set_failure(ctx, "No matching testbenches found for module");
			return msg.str();
		}
	}
	// Case C: Neither testbench nor module specified
	else {
		if (catalog.empty()) {
			set_failure(ctx, "No testbench files found");
			return "Error: No testbench files found in workspace (checked `tb/`, `testbench/`, `tb_*.sv`, `*_tb.sv`). "
			       "Provide a path explicitly via `verilog_simulate(testbench=\"path/to/tb.sv\")`.";
		}
		if (catalog.size() == 1) {
			plans.push_back(make_plan_from_catalog(catalog.front(), catalog.front().modules));
		} else if (!configured_entries.empty() || args_.level_explicit) {
			int max_rank = verilog::fpga_project_settings::test_level_rank(args_.level);
			for (const auto &item : catalog) {
				if (!configured_entries.empty() && !item.explicitly_configured && !args_.level_explicit) {
					continue;
				}
				if (verilog::fpga_project_settings::test_level_rank(item.level) <= max_rank) {
					plans.push_back(make_plan_from_catalog(item, item.modules));
				}
			}
			if (plans.empty()) {
				set_failure(ctx, "No testbenches matched level filter");
				return std::format("No configured testbenches matched `level=\"{}\"`. Try `level=\"extreme\"` or specify `testbench=\"...\"`.",
				                   args_.level);
			}
		} else {
			std::ostringstream msg;
			msg << "Multiple testbench files were found in the project. Specify which one to run via `verilog_simulate(testbench=\"...\")`, "
			       "target a module via `verilog_simulate(module=\"...\")`, or configure testbenches via `verilog_configure_testbench`:\n";
			for (const auto &item : catalog) {
				msg << std::format("- `{}`\n", item.rel_path);
			}
			set_failure(ctx, "Multiple testbenches found; specify testbench or module parameter");
			return msg.str();
		}
	}

	// Phase 1: Compile all planned testbenches in parallel across available CPU cores
	unsigned int hw_threads = std::thread::hardware_concurrency();
	size_t total_cores = static_cast<size_t>(std::max(1u, hw_threads == 0 ? 4u : hw_threads));
	size_t num_workers = std::min(plans.size(), total_cores);
	size_t build_jobs = std::max<size_t>(1, total_cores / std::max<size_t>(1, num_workers));

	std::vector<tb_compile_result> compiled_plans(plans.size());
	const std::string single_override_top = (plans.size() == 1) ? override_top_mod : std::string{};

	if (num_workers <= 1) {
		for (size_t i = 0; i < plans.size(); ++i) {
			compiled_plans[i] = compile_single_testbench(
				root_path, project_root, rtl_files, plans[i], single_override_top, i, build_jobs);
		}
	} else {
		std::atomic<size_t> next_idx{0};
		std::vector<std::jthread> workers;
		workers.reserve(num_workers);
		for (size_t w = 0; w < num_workers; ++w) {
			workers.emplace_back([&]() {
				while (true) {
					size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
					if (idx >= plans.size()) {
						break;
					}
					compiled_plans[idx] = compile_single_testbench(
						root_path, project_root, rtl_files, plans[idx], single_override_top, idx, build_jobs);
				}
			});
		}
	}

	// Phase 2: Execute compiled simulation binaries and VCD post-processing serially
	std::vector<tb_run_outcome> outcomes;
	outcomes.reserve(plans.size());
	size_t pass_count = 0;
	size_t completed_count = 0;
	size_t fail_count = 0;

	for (size_t i = 0; i < plans.size(); ++i) {
		auto outcome = run_compiled_testbench(
			ctx, root_path, project_root, rtl_files, plans[i], compiled_plans[i], args_.timeout_sec);
		if (outcome.suite_failed || outcome.compile_failed || outcome.timed_out || outcome.has_fatal) {
			++fail_count;
		} else if (outcome.suite_passed) {
			++pass_count;
		} else {
			++completed_count;
		}
		outcomes.push_back(std::move(outcome));
	}

	if (outcomes.size() == 1 && args_.requested_module.empty() && !args_.summary_only) {
		const auto &single = outcomes.front();
		if (single.compile_failed || single.timed_out) {
			set_failure(ctx, single.compile_failed ? "Verilator compilation failed" : "Simulation timed out");
		} else {
			set_success(ctx);
		}
		return single.section_markdown;
	}

	// Multi-testbench or module-targeted summary report
	std::ostringstream report;
	if (!args_.requested_module.empty()) {
		std::string root_mod_label = target_hierarchy.empty() ? args_.requested_module : target_hierarchy.back();
		report << std::format("## Module Verification Report: `{}`{}\n\n",
		                      root_mod_label,
		                      effective_hierarchical ? " (Hierarchical)" : "");
		if (hier_from_project_settings) {
			report << std::format("- **Hierarchy Mode**: Ran with `hierarchical=true` due to project settings for `{}` in `turbostar.json` (pass `hierarchical=false` to override)\n",
			                      root_mod_label);
		}
		if (effective_hierarchical && target_hierarchy.size() > 1) {
			std::string chain;
			for (size_t i = 0; i < target_hierarchy.size(); ++i) {
				if (i > 0) chain += " -> ";
				chain += std::format("`{}`", target_hierarchy[i]);
			}
			report << std::format("- **Bottom-Up Module Order**: {}\n", chain);
		}
	} else {
		report << "## Verilog Test Suite Report\n\n";
	}
	report << std::format("- **Level Filter**: `{}`\n", args_.level);
	if (completed_count > 0) {
		report << std::format("- **Summary**: **{} Passed**, **{} Info (no assertions)**, **{} Failed** (out of {} testbench run(s))\n\n",
		                      pass_count, completed_count, fail_count, outcomes.size());
	} else {
		report << std::format("- **Summary**: **{} Passed**, **{} Failed** (out of {} testbench run(s))\n\n",
		                      pass_count, fail_count, outcomes.size());
	}

	report << "| Testbench | Module(s) | Level | Result | Waveform | vs. Previous |\n";
	report << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
	for (const auto &o : outcomes) {
		std::string mods_str;
		for (size_t i = 0; i < o.matched_modules.size(); ++i) {
			if (i > 0) mods_str += ", ";
			mods_str += std::format("`{}`", o.matched_modules[i]);
		}
		if (mods_str.empty()) mods_str = "-";
		std::string status_str = o.compile_failed ? "COMPILE ERROR"
			: (o.timed_out ? "TIMED OUT"
			: (o.suite_failed ? "**FAIL**"
			: (o.suite_passed ? "**PASS**" : "**INFO** (no assertions)")));
		std::string wave_str = o.effective_dump_rel.empty()
			? "*(none)*"
			: std::format("`{}`", o.effective_dump_rel);
		report << std::format("| `{}` | {} | `{}` | {} | {} | {} |\n",
		                      o.display_tb, mods_str, o.level, status_str, wave_str,
		                      o.compare_status.empty() ? "-" : o.compare_status);
	}

	size_t omitted_passing_sections = 0;
	for (const auto &o : outcomes) {
		bool is_notable = o.compile_failed || o.timed_out || o.has_fatal || o.suite_failed ||
		                  o.completed_no_assertions || o.has_collision ||
		                  o.compare_status.starts_with("DIVERGED") ||
		                  o.compare_status.starts_with("LENGTH");
		bool include_section = false;
		if (args_.summary_only) {
			include_section = o.compile_failed || o.timed_out || o.has_fatal || o.suite_failed;
		} else if (args_.verbose || outcomes.size() <= 2) {
			include_section = true;
		} else {
			include_section = is_notable;
		}
		if (include_section) {
			report << "\n---\n\n" << o.section_markdown;
		} else {
			++omitted_passing_sections;
		}
	}
	if (omitted_passing_sections > 0) {
		report << std::format("\n*(Omitted detailed simulation logs for {} passing testbench(es); pass `verbose=true` or `testbench=\"...\"` to inspect full output.)*\n",
		                      omitted_passing_sections);
	}

	if (fail_count > 0) {
		set_failure(ctx, std::format("{} of {} testbench(es) failed", fail_count, outcomes.size()));
	} else {
		set_success(ctx, std::format("{} passed, {} completed", pass_count, completed_count));
	}
	return report.str();
}

} // namespace tools
