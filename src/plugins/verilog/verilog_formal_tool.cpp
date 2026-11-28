#include "plugins/verilog/verilog_formal_tool.h"

#include <algorithm>
#include <chrono>
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
	std::string checked_in_sby_rel; // non-empty if a checked-in formal/<mod>.sby exists on disk
	std::vector<std::string> unchecked_outputs;
	std::vector<sby_task_outcome> tasks;
	std::string raw_error;
	double elapsed_sec{0.0};
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

bool is_ident_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool is_ident_start(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

void collect_identifiers(std::string_view expr, std::set<std::string> &out)
{
	size_t i = 0;
	while (i < expr.size()) {
		// Skip string literals
		if (expr[i] == '"') {
			++i;
			while (i < expr.size() && expr[i] != '"') {
				if (expr[i] == '\\' && i + 1 < expr.size()) {
					++i;
				}
				++i;
			}
			if (i < expr.size()) {
				++i;
			}
			continue;
		}
		// Skip sized/based numeric literals (e.g. 8'hFF, 4'b1010, '0)
		if (expr[i] == '\'') {
			++i;
			while (i < expr.size() && is_ident_char(expr[i])) {
				++i;
			}
			continue;
		}
		if (is_ident_start(expr[i])) {
			// Skip system functions/tasks like $past, $rose, $bits (keep their arguments)
			bool is_sys = (i > 0 && expr[i - 1] == '$');
			size_t start = i;
			while (i < expr.size() && is_ident_char(expr[i])) {
				++i;
			}
			if (!is_sys) {
				out.emplace(expr.substr(start, i - start));
			}
			continue;
		}
		++i;
	}
}

// Extracts the balanced parenthesized expression starting at the first '(' at or after pos.
// Returns empty string_view if not found; advances next_pos past the closing ')'.
std::string_view extract_balanced_parens(std::string_view text, size_t pos, size_t &next_pos)
{
	size_t open = text.find('(', pos);
	if (open == std::string_view::npos) {
		next_pos = text.size();
		return {};
	}
	int depth = 0;
	for (size_t i = open; i < text.size(); ++i) {
		if (text[i] == '"') {
			++i;
			while (i < text.size() && text[i] != '"') {
				if (text[i] == '\\' && i + 1 < text.size()) {
					++i;
				}
				++i;
			}
			continue;
		}
		if (text[i] == '(') {
			++depth;
		} else if (text[i] == ')') {
			--depth;
			if (depth == 0) {
				next_pos = i + 1;
				return text.substr(open + 1, i - open - 1);
			}
		}
	}
	next_pos = text.size();
	return {};
}

// Collect identifiers referenced directly in `assert (...)` or `assert property (...)` conditions
// (excluding any trailing `else $error(...)` diagnostic block).
void collect_assert_identifiers(std::string_view clean_sv, std::set<std::string> &out)
{
	size_t pos = 0;
	while (pos < clean_sv.size()) {
		auto m = ctre::search<R"(\bassert\b(?:\s+(?:property|final|#[0-9]+))?\s*\()">(clean_sv.data() + pos,
		                                                                              clean_sv.data() + clean_sv.size());
		if (!m) {
			break;
		}
		size_t match_offset = static_cast<size_t>(m.data() - clean_sv.data());
		size_t paren_pos = match_offset + m.size() - 1;
		size_t next_pos = clean_sv.size();
		std::string_view cond = extract_balanced_parens(clean_sv, paren_pos, next_pos);
		if (!cond.empty()) {
			collect_identifiers(cond, out);
		}
		pos = std::max(next_pos, match_offset + 6);
	}
}

// Collect lhs -> rhs identifier dependencies from assignments (`assign x = ...;`, `x = ...;`, `x <= ...;`)
// and `bind <mod> ... ( .formal_port(actual_expr) );` port remappings in formal property files.
void collect_formal_dependencies(std::string_view clean_sv,
                                 std::string_view module_name,
                                 std::map<std::string, std::set<std::string>> &deps)
{
	// 1. Assignments inside the formal file: <lhs> = <rhs>; or <lhs> <= <rhs>;
	{
		const char *p = clean_sv.data();
		const char *end = clean_sv.data() + clean_sv.size();
		while (p < end) {
			auto m = ctre::search<R"(\b([a-zA-Z_][a-zA-Z_0-9]*)\s*(?:\[[^\]]*\]\s*)?(?:<=|=(?!=))\s*([^;]+);)">(p, end);
			if (!m) {
				break;
			}
			std::string lhs = m.get<1>().to_string();
			std::string_view rhs = m.get<2>().to_view();
			// Skip keywords like `parameter`/`localparam`? Harmless if recorded, but skip `assert`/`assume`/`cover`
			if (lhs != "assert" && lhs != "assume" && lhs != "cover" && lhs != "if" && lhs != "else") {
				collect_identifiers(rhs, deps[lhs]);
			}
			p = m.data() + m.size();
		}
	}

	// 2. Explicit named port mappings in `bind <module_name> ... ( .formal(actual), ... );`
	{
		size_t pos = 0;
		while (pos < clean_sv.size()) {
			auto m = ctre::search<R"(\bbind\s+([a-zA-Z_][a-zA-Z_0-9]*)\b)">(clean_sv.data() + pos,
			                                                                clean_sv.data() + clean_sv.size());
			if (!m) {
				break;
			}
			size_t match_offset = static_cast<size_t>(m.data() - clean_sv.data());
			size_t after = match_offset + m.size();
			if (m.get<1>().to_view() == module_name) {
				// Skip optional `#(...)` parameter override list before port connection list
				size_t semi = clean_sv.find(';', after);
				std::string_view bind_stmt = (semi == std::string_view::npos)
					? clean_sv.substr(after)
					: clean_sv.substr(after, semi - after);
				const char *bp = bind_stmt.data();
				const char *bend = bind_stmt.data() + bind_stmt.size();
				while (bp < bend) {
					auto pm = ctre::search<R"(\.([a-zA-Z_][a-zA-Z_0-9]*)\s*\()">(bp, bend);
					if (!pm) {
						break;
					}
					std::string formal_port = pm.get<1>().to_string();
					size_t p_off = static_cast<size_t>(pm.data() - bind_stmt.data()) + pm.size() - 1;
					size_t next_p = bind_stmt.size();
					std::string_view actual_expr = extract_balanced_parens(bind_stmt, p_off, next_p);
					if (!actual_expr.empty()) {
						collect_identifiers(actual_expr, deps[formal_port]);
					}
					bp = bind_stmt.data() + std::max(next_p, static_cast<size_t>(pm.data() - bind_stmt.data()) + 1);
				}
			}
			pos = after;
		}
	}
}

} // namespace

std::vector<std::string> verilog_formal_tool::find_unchecked_output_ports(std::string_view module_name,
                                                                          std::span<const std::string> rtl_files,
                                                                          std::span<const std::string> formal_files)
{
	auto ports = verilog::verilog_module_scanner::extract_module_ports(module_name, rtl_files);
	std::vector<std::string> output_ports;
	for (const auto &pt : ports) {
		if (pt.direction == "output" || pt.direction == "inout") {
			output_ports.push_back(pt.name);
		}
	}
	if (output_ports.empty()) {
		return {};
	}

	std::set<std::string> asserted_ids;
	std::map<std::string, std::set<std::string>> deps;

	for (const auto &ff : formal_files) {
		std::string err;
		auto mf = mapped_file::get_cached(ff, err, /*populate=*/false);
		if (!mf) {
			continue;
		}
		std::string clean = verilog::verilog_module_scanner::strip_comments(mf->string_view());
		collect_assert_identifiers(clean, asserted_ids);
		collect_formal_dependencies(clean, module_name, deps);
	}

	// Also scan the module's own RTL file for inline `ifdef FORMAL assertions
	auto discovered = verilog::verilog_module_scanner::discover_modules_in_files(rtl_files);
	for (const auto &dm : discovered) {
		if (dm.module_name == module_name && !dm.source_file.empty()) {
			std::string err;
			auto mf = mapped_file::get_cached(dm.source_file, err, /*populate=*/false);
			if (mf && mf->string_view().find("assert") != std::string_view::npos) {
				std::string clean = verilog::verilog_module_scanner::strip_comments(mf->string_view());
				collect_assert_identifiers(clean, asserted_ids);
			}
			break;
		}
	}

	// Propagate asserted identifiers backward through local helper assignments and bind port remappings
	bool changed = true;
	size_t iter = 0;
	while (changed && iter < 16) {
		changed = false;
		++iter;
		std::vector<std::string> to_add;
		for (const auto &id : asserted_ids) {
			auto it = deps.find(id);
			if (it == deps.end()) {
				continue;
			}
			for (const auto &dep_id : it->second) {
				if (!asserted_ids.contains(dep_id)) {
					to_add.push_back(dep_id);
				}
			}
		}
		if (!to_add.empty()) {
			changed = true;
			for (auto &id : to_add) {
				asserted_ids.insert(std::move(id));
			}
		}
	}

	std::vector<std::string> unchecked;
	for (const auto &out_port : output_ports) {
		if (!asserted_ids.contains(out_port)) {
			unchecked.push_back(out_port);
		}
	}
	return unchecked;
}

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
	if (args_.mode == "all" || args_.mode == "prove+cover") {
		task_list = {"prove", "cover"};
	} else if (args_.mode == "bmc+cover") {
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
		mout.unchecked_outputs = find_unchecked_output_ports(mod, rtl_files, bindings[mod]);

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

		// Check if a checked-in formal/<mod>.sby exists on disk so we can note provenance in the report
		{
			fs::path checked_in_cand = root_path / "formal" / std::format("{}.sby", mod);
			if (fs::is_regular_file(checked_in_cand, ec)) {
				mout.checked_in_sby_rel = std::format("formal/{}.sby", mod);
			}
			ec.clear();
		}

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
				sf << std::format("{}: depth {}\n", t, args_.depth);
			}
			sf << "\n[engines]\n";
			sf << std::format("smtbmc {}\n", solver);

			sf << "\n[script]\n";
			std::string file_args;
			for (const auto &src : mod_sources) {
				file_args += " " + fs::path(src).filename().string();
			}
			if (use_slang) {
				std::string param_args;
				for (const auto &[pk, pv] : args_.parameters) {
					param_args += std::format(" -G {}={}", pk, pv);
				}
				sf << "plugin -i slang\n";
				sf << std::format("read_slang{} --top {} -DFORMAL{}\n", param_args, mod, file_args);
			} else {
				sf << std::format("read -sv -formal{}\n", file_args);
				for (const auto &[pk, pv] : args_.parameters) {
					sf << std::format("chparam -set {} {} {}\n", pk, pv, mod);
				}
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
		auto t_start = std::chrono::steady_clock::now();
		std::string sby_out = fs_utils::execute_command_sync(cmd, args_.timeout_sec);
		auto t_end = std::chrono::steady_clock::now();
		mout.elapsed_sec = std::chrono::duration<double>(t_end - t_start).count();
		mout.timed_out = (sby_out.find("Command timed out") != std::string::npos) ||
		                 (sby_out.find("Process exited with code 137") != std::string::npos) ||
		                 (sby_out.find("Process exited with code 124") != std::string::npos);

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

	// 7. Format Markdown reports (compact inline + full report persisted to system://verilog/formal.md)
	std::ostringstream header;
	header << std::format("## Formal Verification Report (SymbiYosys `smtbmc {}`, depth={})\n\n", solver, args_.depth);
	header << std::format("*Config: auto-generated `.sby` (`{}`)*", use_slang ? "read_slang" : "read -sv -formal");
	if (!args_.parameters.empty()) {
		header << " — *Parameters: ";
		bool first_p = true;
		for (const auto &[pk, pv] : args_.parameters) {
			if (!first_p) header << ", ";
			first_p = false;
			header << std::format("`{}={}`", pk, pv);
		}
		header << "*";
	}
	std::vector<std::string> ignored_checked_in;
	for (const auto &mo : outcomes) {
		if (!mo.checked_in_sby_rel.empty()) {
			ignored_checked_in.push_back(mo.checked_in_sby_rel);
		}
	}
	if (!ignored_checked_in.empty()) {
		header << " — *checked-in ";
		for (size_t i = 0; i < ignored_checked_in.size(); ++i) {
			if (i > 0) header << ", ";
			header << std::format("`{}`", ignored_checked_in[i]);
		}
		header << " not used (auto-generated config applies transitive deps, `$readmem` staging, and solver auto-detection)*";
	}
	header << "\n\n";
	header << "| Module | Property File(s) | Task | Status | Coverpoints | Time | Counterexample VCD |\n";
	header << "|--------|------------------|------|--------|-------------|------|--------------------|\n";

	size_t pass_tasks = 0;
	size_t fail_tasks = 0;
	for (const auto &mo : outcomes) {
		std::string props_str;
		for (size_t i = 0; i < mo.formal_files_rel.size(); ++i) {
			if (i > 0) props_str += ", ";
			props_str += std::format("`{}`", mo.formal_files_rel[i]);
		}
		for (size_t ti = 0; ti < mo.tasks.size(); ++ti) {
			const auto &t = mo.tasks[ti];
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
			std::string time_str = (ti == 0) ? std::format("{:.1f}s", mo.elapsed_sec) : "-";
			std::string vcd_str = t.counterexample_vcd_rel.empty() ? "-" : std::format("`{}`", t.counterexample_vcd_rel);
			header << std::format("| `{}` | {} | `{}` | **{}** | {} | {} | {} |\n",
			                      mo.module_name, props_str, t.task, t.status, cov_str, time_str, vcd_str);
		}
	}

	std::ostringstream report;
	std::ostringstream full_report;
	report << header.str();
	full_report << header.str();

	size_t collapsed_cover_tables = 0;
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
			std::string warn = std::format("\n> [!WARNING] **Vacuity Risk (`{}`)**: Formal proof/BMC passed, but {} `cover()` statement(s) "
			                               "were **UNREACHABLE** within depth {}. Check whether an `assume()` is over-constraining the design.\n",
			                               mo.module_name, unreached_covers, args_.depth);
			report << warn;
			full_report << warn;
		}

		if (!mo.unchecked_outputs.empty()) {
			std::string port_list;
			for (size_t i = 0; i < mo.unchecked_outputs.size(); ++i) {
				if (i > 0) port_list += ", ";
				port_list += std::format("`{}`", mo.unchecked_outputs[i]);
			}
			std::string note = std::format("\n> [!NOTE] **Unchecked Output Ports (`{}`)**: {} {} not referenced by any `assert` in the bound formal properties. "
			                               "Audit whether a wrong value on any clock cycle (including reset/idle) would cause an assertion to fail.\n",
			                               mo.module_name,
			                               port_list,
			                               mo.unchecked_outputs.size() == 1 ? "is" : "are");
			full_report << note;
			if (proof_passed) {
				report << note;
			}
		}

		for (const auto &t : mo.tasks) {
			if (!t.failures.empty()) {
				std::ostringstream fail_sec;
				fail_sec << std::format("\n#### Assertion Failure Context (`{}` / `{}`)\n", mo.module_name, t.task);
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
					fail_sec << std::format("- **{}** at `{}:{}`{}\n", kind_label, disp_f, f.line, step_str);
					if (!snip.empty()) {
						fail_sec << "```systemverilog\n" << snip << "```\n";
					}
				}
				if (!t.counterexample_vcd_rel.empty()) {
					if (is_cli) {
						fail_sec << std::format("- **Inspect Counterexample**: `turbostar-verilog vcd-wave {} -i {}`\n",
						                        t.counterexample_vcd_rel, mo.module_name);
					} else {
						fail_sec << std::format("- **Inspect Counterexample**: `vcd_wave(path=\"{}\", instance=\"{}\")`\n",
						                        t.counterexample_vcd_rel, mo.module_name);
					}
				}
				report << fail_sec.str();
				full_report << fail_sec.str();
			}

			if (!t.covers.empty()) {
				bool has_unreachable = false;
				for (const auto &c : t.covers) {
					if (!c.reached) {
						has_unreachable = true;
						break;
					}
				}
				std::ostringstream cov_sec;
				cov_sec << std::format("\n#### Coverpoints (`{}` / `{}`)\n\n", mo.module_name, t.task);
				cov_sec << "| Location | Status | Statement |\n";
				cov_sec << "|----------|--------|-----------|\n";
				for (const auto &c : t.covers) {
					std::string st = c.reached ? std::format("HIT (step {})", c.step) : "**UNREACHABLE**";
					cov_sec << std::format("| `{}:{}` | {} | `{}` |\n", c.file, c.line, st, c.snippet);
				}
				full_report << cov_sec.str();
				if (has_unreachable || args_.verbose) {
					report << cov_sec.str();
				} else {
					++collapsed_cover_tables;
				}
			}
		}

		if (!mo.raw_error.empty()) {
			std::string err_sec = std::format("\n#### SBY Error Output (`{}`)\n```\n{}```\n", mo.module_name, mo.raw_error);
			report << err_sec;
			full_report << err_sec;
		}
	}

	// Persist full report to <project_tmp>/verilog_formal.md for system://verilog/formal.md
	{
		fs::path formal_report_path = base_tmp / "verilog_formal.md";
		std::ofstream rf(formal_report_path, std::ios::binary | std::ios::trunc);
		if (rf.is_open()) {
			rf << full_report.str();
		}
	}

	if (collapsed_cover_tables > 0) {
		if (is_cli) {
			report << std::format("\n> [!TIP] {} passing coverpoint table(s) collapsed for brevity. "
			                      "Re-run with `--verbose` to print all coverpoints inline.\n",
			                      collapsed_cover_tables);
		} else {
			report << std::format("\n> [!TIP] {} passing coverpoint table(s) collapsed for brevity. "
			                      "Full per-coverpoint breakdown is available at `system://verilog/formal.md` "
			                      "(read with `fs_read_lines(path=\"system://verilog/formal.md\")` or filter with `?search=<module>`).\n",
			                      collapsed_cover_tables);
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
