#include "plugins/verilog/verilog_formal_tool.h"

#include <algorithm>
#include <ctre.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "fs_utils.h"
#include "mapped_file.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/vcd_parser_cache.h"
#include "plugins/verilog/verilog_module_scanner.h"

namespace fs = std::filesystem;

namespace tools
{

namespace
{

struct sby_cover_result {
	std::string file;
	int line{0};
	bool reached{false};
	int step{-1};
	std::string snippet;
};

struct sby_assert_failure {
	std::string task;
	std::string file;
	int line{0};
	int step{-1};
	std::string detail;
};

struct sby_task_outcome {
	std::string task;   // "bmc", "prove", "cover"
	std::string status; // "PASS", "FAIL", "UNKNOWN", "ERROR", "TIMEOUT"
	std::string counterexample_vcd_rel;
	std::vector<sby_assert_failure> failures;
	std::vector<sby_cover_result> covers;
};

struct sby_module_outcome {
	std::string module_name;
	std::vector<std::string> formal_files_rel;
	std::vector<sby_task_outcome> tasks;
	std::string raw_error;
	bool timed_out{false};
};

std::string trim_line_snippet(std::string_view sv)
{
	while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
		sv.remove_suffix(1);
	}
	std::string out;
	out.reserve(sv.size());
	for (char c : sv) {
		if (c == '|') {
			out += "\\|";
		} else {
			out += c;
		}
	}
	constexpr size_t kMaxLen = 100;
	if (out.size() > kMaxLen) {
		return out.substr(0, kMaxLen - 3) + "...";
	}
	return out;
}

fs::path resolve_source_path(std::string_view fname, const fs::path &root_path, std::span<const std::string> all_sources)
{
	std::error_code ec;
	fs::path p(fname);
	if (p.is_absolute() && fs::is_regular_file(p, ec)) {
		return p;
	}
	fs::path rel_cand = root_path / p;
	if (fs::is_regular_file(rel_cand, ec)) {
		return rel_cand;
	}
	std::string base = p.filename().string();
	for (const auto &src : all_sources) {
		fs::path sp(src);
		if (sp.filename().string() == base && fs::is_regular_file(sp, ec)) {
			return sp;
		}
	}
	return {};
}

std::string read_context_snippet(const fs::path &resolved, int fail_line, std::string &out_kind)
{
	out_kind = "Assertion failed";
	if (resolved.empty() || fail_line <= 0) {
		return "";
	}
	std::string err;
	auto mf = mapped_file::get_cached(resolved.string(), err, /*populate=*/false);
	if (!mf) {
		return "";
	}
	auto lines = mf->lines();
	if (static_cast<size_t>(fail_line) > lines.size()) {
		return "";
	}
	std::string_view fail_sv = lines[static_cast<size_t>(fail_line) - 1];
	if (ctre::search<R"(\bassume\b)">(fail_sv)) {
		out_kind = "Formal `assume` failed";
	} else if (ctre::search<R"(\bassert\b)">(fail_sv)) {
		out_kind = "`assert` failed";
	}
	int s_ln = std::max(1, fail_line - 2);
	int e_ln = std::min(static_cast<int>(lines.size()), fail_line + 2);
	std::ostringstream snip;
	for (int ln = s_ln; ln <= e_ln; ++ln) {
		snip << std::format("{} {:>4}: {}\n",
		                    (ln == fail_line ? "-->" : "   "),
		                    ln,
		                    lines[static_cast<size_t>(ln) - 1]);
	}
	return snip.str();
}

std::string read_single_line_snippet(const fs::path &resolved, int ln)
{
	if (resolved.empty() || ln <= 0) {
		return "";
	}
	std::string err;
	auto mf = mapped_file::get_cached(resolved.string(), err, /*populate=*/false);
	if (!mf) {
		return "";
	}
	auto lines = mf->lines();
	if (static_cast<size_t>(ln) > lines.size()) {
		return "";
	}
	return trim_line_snippet(lines[static_cast<size_t>(ln) - 1]);
}

// Discover bind <target_mod> declarations in formal/*.sv files.
std::map<std::string, std::vector<std::string>> discover_formal_bindings(std::span<const std::string> formal_files)
{
	std::map<std::string, std::vector<std::string>> bindings;
	for (const auto &ff : formal_files) {
		std::string err;
		auto mf = mapped_file::get_cached(ff, err, /*populate=*/false);
		if (!mf) {
			continue;
		}
		std::string clean = verilog::verilog_module_scanner::strip_comments(mf->string_view());
		const char *start = clean.data();
		const char *end = clean.data() + clean.size();
		std::set<std::string> targets_in_file;
		while (start < end) {
			auto m = ctre::search<R"(\bbind\s+([a-zA-Z_][a-zA-Z_0-9]*)\b)">(start, end);
			if (!m) {
				break;
			}
			targets_in_file.insert(m.get<1>().to_string());
			start = m.data() + m.size();
		}
		for (const auto &t : targets_in_file) {
			bindings[t].push_back(ff);
		}
	}
	return bindings;
}

std::string find_tool_with_prefix(const fs::path &sby_dir, std::string_view name)
{
	std::error_code ec;
	if (!sby_dir.empty()) {
		fs::path cand = sby_dir / name;
		if (fs::exists(cand, ec) && ::access(cand.c_str(), X_OK) == 0) {
			return cand.string();
		}
	}
	return fs_utils::find_executable(name);
}

std::string detect_smt_solver(const fs::path &sby_dir, std::string_view requested)
{
	if (requested != "auto") {
		std::string exe = (requested == "yices") ? "yices-smt2" : std::string(requested);
		return find_tool_with_prefix(sby_dir, exe).empty() ? "" : std::string(requested);
	}
	if (!find_tool_with_prefix(sby_dir, "bitwuzla").empty()) return "bitwuzla";
	if (!find_tool_with_prefix(sby_dir, "yices-smt2").empty()) return "yices";
	if (!find_tool_with_prefix(sby_dir, "z3").empty()) return "z3";
	return "";
}

bool yosys_supports_slang(const fs::path &sby_dir)
{
	std::string yosys_bin = find_tool_with_prefix(sby_dir, "yosys");
	if (yosys_bin.empty()) {
		return false;
	}
	std::string cmd = std::format("{} -m slang -q -p \"help read_slang\" >/dev/null 2>&1 && echo SLANG_OK",
	                              fs_utils::escape_shell_arg(yosys_bin));
	std::string out = fs_utils::execute_command_sync(cmd, 10);
	return out.find("SLANG_OK") != std::string::npos;
}

} // namespace

verilog_formal_tool::verilog_formal_tool(verilog_formal_args args)
	: agentlib::llm_tool_action("Running SymbiYosys formal verification"),
	  args_(std::move(args))
{
}

bool verilog_formal_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_formal_tool::execute(agentlib::tool_context &ctx)
{
	std::error_code ec;
	std::string project_root = ctx.fs_security.get_working_directory();
	if (project_root.empty()) {
		project_root = fs::current_path(ec).string();
	}
	fs::path root_path = fs::weakly_canonical(project_root, ec);
	if (ec) {
		root_path = project_root;
		ec.clear();
	}

	verilog::fpga_target_config cfg;
	std::string cfg_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, cfg_err);

	// 1. Locate sby (fpga.sby_path in turbostar.json, then $PATH with ~/ expansion)
	std::string sby_bin;
	if (!cfg.sby_path.empty()) {
		sby_bin = fs_utils::find_executable(cfg.sby_path);
	}
	if (sby_bin.empty()) {
		sby_bin = fs_utils::find_executable("sby");
	}
	if (sby_bin.empty()) {
		set_failure(ctx, "sby not found in PATH");
		return "Error: `sby` (SymbiYosys) was not found in PATH or `fpga.sby_path`.\n"
		       "Install OSS CAD Suite (which includes `sby`, `yosys` with `slang`, and `bitwuzla`/`z3`) "
		       "and add its `bin/` directory to PATH, or set `\"sby_path\"` under `\"fpga\"` in `turbostar.json`.";
	}

	fs::path sby_dir = fs::path(sby_bin).parent_path();
	std::string solver = detect_smt_solver(sby_dir, args_.solver);
	if (solver.empty()) {
		set_failure(ctx, "No SMT solver found for sby");
		return std::format("Error: No compatible SMT solver found (requested: `{}`). "
		                   "Install `bitwuzla`, `yices-smt2`, or `z3` alongside `sby`.",
		                   args_.solver);
	}

	// 2. Discover RTL files and formal/*_props.sv bindings
	fs::path scan_root = cfg.sv_path.empty()
		? (root_path / verilog::fpga_project_settings::detect_sv_path(project_root))
		: (root_path / cfg.sv_path);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
		ec.clear();
	}
	std::vector<std::string> rtl_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
	std::vector<std::string> formal_files = verilog::verilog_module_scanner::find_formal_files(root_path);
	auto discovered_mods = verilog::verilog_module_scanner::discover_modules_in_files(rtl_files);
	auto bindings = discover_formal_bindings(formal_files);

	if (formal_files.empty()) {
		set_failure(ctx, "No formal/*.sv property files found");
		return "Error: No formal property files found under `formal/`. "
		       "Create `formal/<module>_props.sv` with a `bind <module> ...` statement to run `verilog_formal`.";
	}

	// Determine target modules to verify
	std::vector<std::string> target_modules;
	if (!args_.requested_module.empty()) {
		std::string resolved_mod = verilog::verilog_module_scanner::resolve_module_name(args_.requested_module, "", rtl_files);
		if (resolved_mod.empty()) {
			resolved_mod = args_.requested_module;
		}
		if (!bindings.contains(resolved_mod)) {
			std::ostringstream msg;
			msg << std::format("Error: No `bind {} ...` property module found in `formal/`.\n", resolved_mod);
			if (!bindings.empty()) {
				msg << "Modules with formal bindings:\n";
				for (const auto &[m, _] : bindings) {
					msg << std::format("- `{}`\n", m);
				}
			}
			set_failure(ctx, "No formal property binding for requested module");
			return msg.str();
		}
		target_modules.push_back(resolved_mod);
	} else {
		for (const auto &[m, _] : bindings) {
			target_modules.push_back(m);
		}
		if (target_modules.empty()) {
			set_failure(ctx, "No bind statements found in formal/*.sv");
			return "Error: Found files in `formal/`, but none contain a `bind <module> ...` statement.";
		}
	}

	std::map<std::string, std::string> mod_to_file;
	for (const auto &dm : discovered_mods) {
		mod_to_file[dm.module_name] = dm.source_file;
	}

	const bool use_slang = yosys_supports_slang(sby_dir);
	const bool is_cli = verilog::fpga_project_settings::is_cli_mode();

	std::vector<std::string> task_list;
	if (args_.mode == "all") {
		task_list = {"bmc", "cover"};
	} else {
		task_list = {args_.mode};
	}

	fs::path base_tmp = (fs::weakly_canonical(root_path, ec) == fs::weakly_canonical(fs::current_path(ec), ec))
		? fs::path(fs_utils::get_project_tmp_dir())
		: (root_path / ".turbostar_tmp");
	ec.clear();
	fs::path formal_work_root = fs::absolute(base_tmp / "sby_formal", ec);
	fs::create_directories(formal_work_root, ec);

	const char *orig_path = std::getenv("PATH");
	std::string path_prefix = sby_dir.string();
	if (orig_path && *orig_path) {
		path_prefix += ":";
		path_prefix += orig_path;
	}

	std::vector<sby_module_outcome> outcomes;
	outcomes.reserve(target_modules.size());

	for (const auto &mod : target_modules) {
		sby_module_outcome mout;
		mout.module_name = mod;

		// 3. Collect transitive RTL files + bound formal files for this module
		std::vector<std::string> hier = verilog::verilog_module_scanner::discover_submodule_hierarchy(mod, rtl_files);
		std::set<std::string> needed_mods(hier.begin(), hier.end());
		needed_mods.insert(mod);

		std::vector<std::string> mod_sources;
		std::set<std::string> seen_src;
		for (const auto &dm : discovered_mods) {
			if (needed_mods.contains(dm.module_name) && seen_src.insert(dm.source_file).second) {
				mod_sources.push_back(dm.source_file);
			}
		}
		if (mod_sources.empty()) {
			mod_sources = rtl_files;
		}
		for (const auto &ff : bindings[mod]) {
			if (seen_src.insert(ff).second) {
				mod_sources.push_back(ff);
			}
			fs::path rel = fs::relative(ff, root_path, ec);
			mout.formal_files_rel.push_back((!ec && !rel.empty()) ? rel.generic_string() : ff);
			ec.clear();
		}

		fs::path mod_work = formal_work_root / mod;
		fs::remove_all(mod_work, ec);
		ec.clear();
		fs::create_directories(mod_work, ec);

		// Stage $readmemh/$readmemb data files referenced by transitive RTL
		verilog::verilog_module_scanner::stage_readmem_files(mod_sources, mod_work);

		// 4. Generate .sby file
		fs::path sby_file = mod_work / std::format("{}.sby", mod);
		{
			std::ofstream sf(sby_file, std::ios::binary | std::ios::trunc);
			sf << "[tasks]\n";
			for (const auto &t : task_list) {
				sf << t << "\n";
			}
			sf << "\n[options]\n";
			for (const auto &t : task_list) {
				sf << std::format("{}: mode {}\n", t, t);
				if (t == "bmc" || t == "cover") {
					sf << std::format("{}: depth {}\n", t, args_.depth);
				}
			}
			sf << "\n[engines]\n";
			sf << std::format("smtbmc {}\n", solver);

			sf << "\n[script]\n";
			std::string file_args;
			for (const auto &src : mod_sources) {
				file_args += " " + fs::path(src).filename().string();
			}
			if (use_slang) {
				sf << "plugin -i slang\n";
				sf << std::format("read_slang --top {} -DFORMAL{}\n", mod, file_args);
			} else {
				sf << std::format("read -sv -formal{}\n", file_args);
			}
			sf << std::format("prep -top {}\n", mod);

			sf << "\n[files]\n";
			for (const auto &src : mod_sources) {
				sf << fs::weakly_canonical(src, ec).string() << "\n";
				ec.clear();
			}
			// Include staged $readmem data files (relative to mod_work) so SBY copies them into <task>/src/
			for (const auto &entry : fs::recursive_directory_iterator(mod_work, ec)) {
				if (entry.is_regular_file(ec) && entry.path() != sby_file) {
					fs::path rel_data = fs::relative(entry.path(), mod_work, ec);
					if (!ec && !rel_data.empty()) {
						sf << std::format("{} {}\n", rel_data.generic_string(), entry.path().string());
					}
					ec.clear();
				}
			}
		}

		// 5. Execute sby
		std::string cmd = std::format(
			"cd {} && PATH={} {} -f {} 2>&1",
			fs_utils::escape_shell_arg(mod_work.string()),
			fs_utils::escape_shell_arg(path_prefix),
			fs_utils::escape_shell_arg(sby_bin),
			fs_utils::escape_shell_arg(sby_file.filename().string())
		);
		std::string sby_out = fs_utils::execute_command_sync(cmd, args_.timeout_sec);
		mout.timed_out = sby_out.find("Command timed out") != std::string::npos;

		// 6. Parse SBY output per task
		std::map<std::string, sby_task_outcome> task_map;
		for (const auto &t : task_list) {
			sby_task_outcome to;
			to.task = t;
			to.status = mout.timed_out ? "TIMEOUT" : "ERROR";
			task_map[t] = std::move(to);
		}

		std::map<std::string, int> task_step;
		std::string_view sby_view(sby_out);
		size_t pos = 0;
		while (pos < sby_view.size()) {
			size_t nl = sby_view.find('\n', pos);
			std::string_view line = (nl == std::string_view::npos) ? sby_view.substr(pos) : sby_view.substr(pos, nl - pos);
			pos = (nl == std::string_view::npos) ? sby_view.size() : (nl + 1);
			if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

			// Identify which task bracket this line belongs to: [<mod>_<task>]
			std::string matched_task;
			for (const auto &t : task_list) {
				std::string tag = std::format("[{}_{}]", mod, t);
				if (line.find(tag) != std::string_view::npos) {
					matched_task = t;
					break;
				}
			}
			if (matched_task.empty()) {
				if ((line.find("ERROR") != std::string_view::npos ||
				     line.find("Error") != std::string_view::npos ||
				     line.find("Traceback") != std::string_view::npos ||
				     line.find("Permission denied") != std::string_view::npos ||
				     line.find("not found") != std::string_view::npos) &&
				    mout.raw_error.size() < 1200) {
					mout.raw_error += std::string(line) + "\n";
				}
				continue;
			}
			auto &to = task_map[matched_task];

			if (auto dm = ctre::search<R"(DONE \((PASS|FAIL|UNKNOWN|ERROR))">(line)) {
				to.status = dm.get<1>().to_string();
			} else if (auto sm = ctre::search<R"(Checking (?:assertions|assumptions) in step (\d+))">(line)) {
				try { task_step[matched_task] = std::stoi(sm.get<1>().to_string()); } catch (...) {}
			} else if (auto am = ctre::search<R"(Assert failed in [^:]+:\s*([^:\s]+):(\d+))">(line)) {
				sby_assert_failure af;
				af.task = matched_task;
				af.file = am.get<1>().to_string();
				try { af.line = std::stoi(am.get<2>().to_string()); } catch (...) {}
				if (auto it = task_step.find(matched_task); it != task_step.end()) {
					af.step = it->second;
				}
				af.detail = std::string(line);
				to.failures.push_back(std::move(af));
			} else if (auto cm = ctre::search<R"(Reached cover statement in step (\d+) at [^:]+:\s*([^:\s]+):(\d+))">(line)) {
				sby_cover_result cr;
				try { cr.step = std::stoi(cm.get<1>().to_string()); } catch (...) {}
				cr.file = cm.get<2>().to_string();
				try { cr.line = std::stoi(cm.get<3>().to_string()); } catch (...) {}
				cr.reached = true;
				fs::path resolved = resolve_source_path(cr.file, root_path, mod_sources);
				cr.snippet = read_single_line_snippet(resolved, cr.line);
				to.covers.push_back(std::move(cr));
			} else if (auto um = ctre::search<R"(Unreached cover statement at [^:]+:\s*([^:\s]+):(\d+))">(line)) {
				sby_cover_result cr;
				cr.file = um.get<1>().to_string();
				try { cr.line = std::stoi(um.get<2>().to_string()); } catch (...) {}
				cr.reached = false;
				fs::path resolved = resolve_source_path(cr.file, root_path, mod_sources);
				cr.snippet = read_single_line_snippet(resolved, cr.line);
				to.covers.push_back(std::move(cr));
			} else if (auto vm = ctre::search<R"(Writing trace to VCD file:\s*(\S+))">(line)) {
				// Copy counterexample VCD on bmc/prove failure into wave/ for vcd_wave inspection
				if (matched_task != "cover" && to.counterexample_vcd_rel.empty()) {
					fs::path src_vcd = mod_work / std::format("{}_{}", mod, matched_task) / vm.get<1>().to_string();
					if (fs::is_regular_file(src_vcd, ec)) {
						fs::path wave_dir = root_path / "wave";
						fs::create_directories(wave_dir, ec);
						fs::path dst_vcd = wave_dir / std::format("formal_{}_{}.vcd", mod, matched_task);
						fs::copy_file(src_vcd, dst_vcd, fs::copy_options::overwrite_existing, ec);
						if (!ec) {
							to.counterexample_vcd_rel = std::format("wave/formal_{}_{}.vcd", mod, matched_task);
							std::string perr;
							verilog::vcd_parser_cache::get_instance().get_or_parse(dst_vcd.string(), perr);
						}
						ec.clear();
					}
				}
			} else if (line.find("ERROR:") != std::string_view::npos && mout.raw_error.size() < 800) {
				mout.raw_error += std::string(line) + "\n";
			}
		}

		bool any_error = false;
		for (const auto &t : task_list) {
			auto &to = task_map[t];
			std::sort(to.covers.begin(), to.covers.end(), [](const sby_cover_result &a, const sby_cover_result &b) {
				if (a.file != b.file) return a.file < b.file;
				return a.line < b.line;
			});
			if (to.status == "ERROR") any_error = true;
			mout.tasks.push_back(std::move(to));
		}
		if (any_error && mout.raw_error.empty() && !sby_out.empty()) {
			size_t tail_start = (sby_out.size() > 1200) ? (sby_out.size() - 1200) : 0;
			if (tail_start > 0) {
				size_t nl = sby_out.find('\n', tail_start);
				if (nl != std::string::npos) tail_start = nl + 1;
			}
			mout.raw_error = sby_out.substr(tail_start);
			if (!mout.raw_error.empty() && mout.raw_error.back() != '\n') {
				mout.raw_error += '\n';
			}
		}
		outcomes.push_back(std::move(mout));
	}

	// 7. Format Markdown report
	std::ostringstream report;
	report << std::format("## Formal Verification Report (SymbiYosys `smtbmc {}`, depth={})\n\n", solver, args_.depth);
	report << "| Module | Property File(s) | Task | Status | Coverpoints | Counterexample VCD |\n";
	report << "|--------|------------------|------|--------|-------------|--------------------|\n";

	size_t pass_tasks = 0;
	size_t fail_tasks = 0;
	for (const auto &mo : outcomes) {
		std::string props_str;
		for (size_t i = 0; i < mo.formal_files_rel.size(); ++i) {
			if (i > 0) props_str += ", ";
			props_str += std::format("`{}`", mo.formal_files_rel[i]);
		}
		for (const auto &t : mo.tasks) {
			if (t.status == "PASS") ++pass_tasks;
			else ++fail_tasks;

			std::string cov_str = "-";
			if (!t.covers.empty()) {
				size_t hit = 0;
				for (const auto &c : t.covers) {
					if (c.reached) ++hit;
				}
				cov_str = std::format("{}/{} hit", hit, t.covers.size());
			}
			std::string vcd_str = t.counterexample_vcd_rel.empty() ? "-" : std::format("`{}`", t.counterexample_vcd_rel);
			report << std::format("| `{}` | {} | `{}` | **{}** | {} | {} |\n",
			                      mo.module_name, props_str, t.task, t.status, cov_str, vcd_str);
		}
	}

	// Detailed per-module diagnostics (failures, coverpoint tables, vacuity warnings)
	for (const auto &mo : outcomes) {
		bool proof_passed = false;
		size_t unreached_covers = 0;
		for (const auto &t : mo.tasks) {
			if ((t.task == "bmc" || t.task == "prove") && t.status == "PASS") {
				proof_passed = true;
			}
			for (const auto &c : t.covers) {
				if (!c.reached) ++unreached_covers;
			}
		}

		if (proof_passed && unreached_covers > 0) {
			report << std::format("\n> [!WARNING] **Vacuity Risk (`{}`)**: Formal proof/BMC passed, but {} `cover()` statement(s) "
			                      "were **UNREACHABLE** within depth {}. Check whether an `assume()` is over-constraining the design.\n",
			                      mo.module_name, unreached_covers, args_.depth);
		}

		for (const auto &t : mo.tasks) {
			if (!t.failures.empty()) {
				report << std::format("\n#### Assertion Failure Context (`{}` / `{}`)\n", mo.module_name, t.task);
				for (const auto &f : t.failures) {
					std::vector<std::string> search_srcs = rtl_files;
					search_srcs.insert(search_srcs.end(), formal_files.begin(), formal_files.end());
					fs::path resolved = resolve_source_path(f.file, root_path, search_srcs);
					std::string disp_f = f.file;
					if (!resolved.empty()) {
						fs::path rel = fs::relative(resolved, root_path, ec);
						if (!ec && !rel.empty()) disp_f = rel.generic_string();
						ec.clear();
					}
					std::string kind_label;
					std::string snip = read_context_snippet(resolved, f.line, kind_label);
					std::string step_str = (f.step >= 0) ? std::format(" (step {})", f.step) : "";
					report << std::format("- **{}** at `{}:{}`{}\n", kind_label, disp_f, f.line, step_str);
					if (!snip.empty()) {
						report << "```systemverilog\n" << snip << "```\n";
					}
				}
				if (!t.counterexample_vcd_rel.empty()) {
					if (is_cli) {
						report << std::format("- **Inspect Counterexample**: `turbostar-verilog vcd-wave {}`\n",
						                      t.counterexample_vcd_rel);
					} else {
						report << std::format("- **Inspect Counterexample**: `vcd_wave(path=\"{}\")`\n",
						                      t.counterexample_vcd_rel);
					}
				}
			}

			if (!t.covers.empty()) {
				report << std::format("\n#### Coverpoints (`{}` / `{}`)\n\n", mo.module_name, t.task);
				report << "| Location | Status | Statement |\n";
				report << "|----------|--------|-----------|\n";
				for (const auto &c : t.covers) {
					std::string st = c.reached ? std::format("HIT (step {})", c.step) : "**UNREACHABLE**";
					report << std::format("| `{}:{}` | {} | `{}` |\n", c.file, c.line, st, c.snippet);
				}
			}
		}

		if (!mo.raw_error.empty()) {
			report << std::format("\n#### SBY Error Output (`{}`)\n```\n{}```\n", mo.module_name, mo.raw_error);
		}
	}

	if (fail_tasks > 0) {
		set_failure(ctx, std::format("{} of {} formal task(s) failed", fail_tasks, pass_tasks + fail_tasks));
	} else {
		set_success(ctx, std::format("{} formal task(s) passed", pass_tasks));
	}
	return report.str();
}

} // namespace tools
