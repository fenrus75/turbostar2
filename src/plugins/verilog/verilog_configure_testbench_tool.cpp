#include "plugins/verilog/verilog_configure_testbench_tool.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <sstream>
#include "fs_utils.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_module_scanner.h"

namespace tools
{

namespace fs = std::filesystem;

verilog_configure_testbench_tool::verilog_configure_testbench_tool(verilog_configure_testbench_args args)
	: agentlib::llm_tool_action("Configuring Verilog testbench associations"),
	  args_(std::move(args))
{
}

bool verilog_configure_testbench_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_configure_testbench_tool::execute(agentlib::tool_context &ctx)
{
	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = ".";
	}
	fs::path root_path(project_root);
	std::error_code ec;

	verilog::fpga_target_config cfg;
	std::string cfg_err;
	verilog::fpga_project_settings::load_from_project(project_root, cfg, cfg_err);

	fs::path scan_root = cfg.sv_path.empty()
		? (root_path / verilog::fpga_project_settings::detect_sv_path(project_root))
		: (root_path / cfg.sv_path);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
	}
	std::vector<std::string> rtl_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);

	std::vector<verilog::testbench_entry> entries;
	std::string config_path;
	std::string load_err;
	if (!verilog::fpga_project_settings::load_testbenches(project_root, entries, config_path, load_err)) {
		set_failure(ctx, load_err);
		return std::format("Error loading testbench configuration: {}", load_err);
	}

	auto normalize_tb_rel = [&](std::string_view path_str) -> std::string {
		fs::path p(path_str);
		if (!p.is_absolute()) {
			p = root_path / p;
		}
		fs::path canon = fs::weakly_canonical(p, ec);
		fs::path canon_root = fs::weakly_canonical(root_path, ec);
		fs::path rel = fs::relative(canon, canon_root, ec);
		if (!ec && !rel.empty() && !rel.generic_string().starts_with("..")) {
			return rel.generic_string();
		}
		return fs::path(path_str).generic_string();
	};

	auto resolve_req_mod = [&](const std::string &req_m) -> std::string {
		std::string safe_mod_file;
		std::string dummy_err;
		if (ctx.fs_security.validate_access(req_m, agentlib::access_type::read, safe_mod_file, dummy_err) &&
		    fs_utils::is_regular_file(safe_mod_file)) {
			std::string resolved = verilog::verilog_module_scanner::resolve_module_name("", safe_mod_file, rtl_files);
			if (!resolved.empty()) {
				return resolved;
			}
		}
		return verilog::verilog_module_scanner::resolve_module_name(req_m, "", rtl_files);
	};

	std::vector<std::string> resolved_modules;
	for (const auto &req_m : args_.requested_modules) {
		std::string rm = resolve_req_mod(req_m);
		if (!rm.empty() && std::find(resolved_modules.begin(), resolved_modules.end(), rm) == resolved_modules.end()) {
			resolved_modules.push_back(std::move(rm));
		}
	}

	std::string action_summary;
	bool modified = false;

	if (args_.remove) {
		if (!args_.requested_testbench.empty()) {
			std::string target_rel = normalize_tb_rel(args_.safe_testbench.empty() ? args_.requested_testbench : args_.safe_testbench);
			auto it = std::find_if(entries.begin(), entries.end(), [&](const verilog::testbench_entry &e) {
				return normalize_tb_rel(e.testbench) == target_rel || e.testbench == args_.requested_testbench;
			});
			if (it == entries.end()) {
				action_summary = std::format("No configured entry found for testbench `{}` (nothing to remove).", target_rel);
			} else if (resolved_modules.empty()) {
				entries.erase(it);
				modified = true;
				action_summary = std::format("Removed testbench `{}` from configuration.", target_rel);
			} else {
				size_t removed_cnt = 0;
				for (const auto &rm : resolved_modules) {
					auto mit = std::find(it->modules.begin(), it->modules.end(), rm);
					if (mit != it->modules.end()) {
						it->modules.erase(mit);
						++removed_cnt;
					}
				}
				if (it->modules.empty()) {
					entries.erase(it);
					action_summary = std::format("Removed last module association(s) and deleted testbench entry `{}`.", target_rel);
				} else {
					action_summary = std::format("Removed {} module association(s) from testbench `{}`.", removed_cnt, target_rel);
				}
				modified = true;
			}
		} else if (!resolved_modules.empty()) {
			size_t removed_cnt = 0;
			for (auto it = entries.begin(); it != entries.end();) {
				for (const auto &rm : resolved_modules) {
					auto mit = std::find(it->modules.begin(), it->modules.end(), rm);
					if (mit != it->modules.end()) {
						it->modules.erase(mit);
						++removed_cnt;
					}
				}
				if (it->modules.empty()) {
					it = entries.erase(it);
				} else {
					++it;
				}
			}
			modified = true;
			action_summary = std::format("Removed module(s) from {} testbench association(s).", removed_cnt);
		} else {
			size_t count = entries.size();
			entries.clear();
			modified = true;
			action_summary = std::format("Cleared all {} configured testbench association(s).", count);
		}
	} else if (!args_.requested_testbench.empty()) {
		std::string target_rel = normalize_tb_rel(args_.safe_testbench);
		auto it = std::find_if(entries.begin(), entries.end(), [&](const verilog::testbench_entry &e) {
			return normalize_tb_rel(e.testbench) == target_rel;
		});

		if (resolved_modules.empty()) {
			resolved_modules = verilog::verilog_module_scanner::discover_instantiated_modules(args_.safe_testbench, rtl_files);
		}

		if (it == entries.end()) {
			verilog::testbench_entry new_entry;
			new_entry.testbench = target_rel;
			new_entry.modules = resolved_modules;
			new_entry.level = args_.level.empty() ? "quick" : args_.level;
			entries.push_back(std::move(new_entry));
			action_summary = std::format("Configured testbench `{}` (level: `{}`).",
			                             target_rel, args_.level.empty() ? "quick" : args_.level);
		} else {
			it->testbench = target_rel;
			for (const auto &rm : resolved_modules) {
				if (std::find(it->modules.begin(), it->modules.end(), rm) == it->modules.end()) {
					it->modules.push_back(rm);
				}
			}
			if (!args_.level.empty()) {
				it->level = args_.level;
			}
			action_summary = std::format("Updated testbench `{}` (level: `{}`).", target_rel, it->level);
		}
		modified = true;
	}

	std::string saved_path = config_path;
	if (modified) {
		std::string save_err;
		if (!verilog::fpga_project_settings::save_testbenches(project_root, entries, saved_path, save_err)) {
			set_failure(ctx, save_err);
			return std::format("Error saving testbench configuration: {}", save_err);
		}
	}

	// Discover all testbenches on disk to show both configured and unconfigured testbenches and check VCD collisions
	std::vector<std::string> disk_tbs = verilog::verilog_module_scanner::find_testbench_files(root_path);
	std::set<std::string> configured_rels;
	std::map<std::string, std::vector<std::string>> dumpfile_to_tbs;

	std::ostringstream ss;
	ss << "### Verilog Testbench Configuration\n\n";
	if (!action_summary.empty()) {
		ss << "- **Action**: " << action_summary << "\n";
	}
	if (!saved_path.empty()) {
		fs::path rel_cfg = fs::relative(saved_path, root_path, ec);
		ss << std::format("- **Config File**: `{}`\n", (!ec && !rel_cfg.empty()) ? rel_cfg.generic_string() : saved_path);
	}

	ss << "\n#### Configured Testbenches\n";
	if (entries.empty()) {
		ss << "*No testbenches explicitly configured in `turbostar.json`.*\n";
	} else {
		ss << "| Testbench | Associated Modules | Level | `$dumpfile` VCD |\n";
		ss << "| :--- | :--- | :--- | :--- |\n";
		for (const auto &e : entries) {
			std::string rel_tb = normalize_tb_rel(e.testbench);
			configured_rels.insert(rel_tb);
			fs::path full_tb = root_path / rel_tb;
			bool exists_on_disk = fs::exists(full_tb, ec);
			std::string raw_dump = exists_on_disk
				? verilog::verilog_module_scanner::extract_dumpfile_path(full_tb)
				: "";
			if (exists_on_disk && !raw_dump.empty()) {
				dumpfile_to_tbs[raw_dump].push_back(rel_tb);
			}
			std::string dump_display = !exists_on_disk
				? "*(missing file)*"
				: (raw_dump.empty() ? "*(none)*" : std::format("`{}`", raw_dump));

			std::string mods_str;
			for (size_t i = 0; i < e.modules.size(); ++i) {
				if (i > 0) {
					mods_str += ", ";
				}
				mods_str += std::format("`{}`", e.modules[i]);
			}
			if (mods_str.empty()) {
				mods_str = "*none*";
			}
			ss << std::format("| `{}` | {} | `{}` | {} |\n", rel_tb, mods_str, e.level, dump_display);
		}
	}

	std::vector<std::string> unconfigured_tbs;
	for (const auto &dtb : disk_tbs) {
		std::string rel_tb = normalize_tb_rel(dtb);
		if (!configured_rels.contains(rel_tb)) {
			unconfigured_tbs.push_back(dtb);
			std::string dump_path = verilog::verilog_module_scanner::extract_dumpfile_path(dtb);
			if (!dump_path.empty()) {
				dumpfile_to_tbs[dump_path].push_back(rel_tb);
			}
		}
	}

	if (!unconfigured_tbs.empty()) {
		ss << "\n#### Unconfigured Workspace Testbenches (Auto-Discovered)\n";
		ss << "| Testbench | Auto-Detected Instantiated Modules | `$dumpfile` VCD |\n";
		ss << "| :--- | :--- | :--- |\n";
		for (const auto &dtb : unconfigured_tbs) {
			std::string rel_tb = normalize_tb_rel(dtb);
			auto auto_mods = verilog::verilog_module_scanner::discover_instantiated_modules(dtb, rtl_files);
			std::string mods_str;
			for (size_t i = 0; i < auto_mods.size(); ++i) {
				if (i > 0) {
					mods_str += ", ";
				}
				mods_str += std::format("`{}`", auto_mods[i]);
			}
			if (mods_str.empty()) {
				mods_str = "*none detected*";
			}
			std::string dump_path = verilog::verilog_module_scanner::extract_dumpfile_path(dtb);
			std::string dump_display = dump_path.empty() ? "*(none)*" : std::format("`{}`", dump_path);
			ss << std::format("| `{}` | {} | {} |\n", rel_tb, mods_str, dump_display);
		}
	}

	// Check for VCD filename collisions across testbenches
	auto suggest_unique_dumpfile = [](std::string_view raw_dump, std::string_view tb_rel) -> std::string {
		fs::path raw_p(raw_dump);
		std::string stem = fs::path(tb_rel).stem().string();
		if (stem.starts_with("tb_") && stem.size() > 3) {
			stem = stem.substr(3);
		} else if (stem.ends_with("_tb") && stem.size() > 3) {
			stem = stem.substr(0, stem.size() - 3);
		}
		fs::path suggested = raw_p.parent_path() / std::format("dump_{}.vcd", stem.empty() ? "tb" : stem);
		return suggested.generic_string();
	};

	bool has_collision = false;
	for (const auto &[dump_path, tb_list] : dumpfile_to_tbs) {
		if (tb_list.size() > 1) {
			if (!has_collision) {
				ss << "\n> [!WARNING] **Waveform `$dumpfile` Collision Detected — Recommended Fix**\n";
				has_collision = true;
			}
			std::string tbs_joined;
			for (size_t i = 0; i < tb_list.size(); ++i) {
				if (i > 0) {
					tbs_joined += ", ";
				}
				tbs_joined += std::format("`{}`", tb_list[i]);
			}
			ss << std::format("> Multiple testbenches write to `{}`: {}.\n", dump_path, tbs_joined);
			ss << "> **Recommended Fix**: Edit the colliding testbenches so each writes to a unique `$dumpfile` path:\n";
			for (const auto &tb_rel : tb_list) {
				ss << std::format("> - In `{}`: rename `$dumpfile(\"{}\")` -> `$dumpfile(\"{}\")`\n",
				                  tb_rel, dump_path, suggest_unique_dumpfile(dump_path, tb_rel));
			}
		}
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
