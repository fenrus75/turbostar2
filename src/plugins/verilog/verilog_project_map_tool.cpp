#include "plugins/verilog/verilog_project_map_tool.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_module_scanner.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace tools {

verilog_project_map_tool::verilog_project_map_tool(verilog_project_map_args args)
	: agentlib::llm_tool_action("Mapping Verilog project hierarchy"), args_(std::move(args))
{
}

bool verilog_project_map_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string verilog_project_map_tool::execute(agentlib::tool_context &ctx)
{
	std::string project_root = ctx.fs_security.get_working_directory().string();
	if (project_root.empty()) {
		project_root = fs::current_path().string();
	}

	std::error_code ec;
	fs::path root_path = fs::weakly_canonical(project_root, ec);
	if (ec) {
		root_path = fs::path(project_root);
	}

	auto to_rel = [&](const fs::path &p) -> std::string {
		fs::path canon = fs::weakly_canonical(p, ec);
		fs::path rel = fs::relative(ec ? p : canon, root_path, ec);
		if (!ec && !rel.empty() && !rel.generic_string().starts_with("..")) {
			return rel.generic_string();
		}
		return p.generic_string();
	};

	verilog::fpga_target_config cfg;
	std::string cfg_err;
	bool has_cfg = verilog::fpga_project_settings::load_from_project(project_root, cfg, cfg_err);

	std::string sv_rel = cfg.sv_path.empty()
		? verilog::fpga_project_settings::detect_sv_path(project_root)
		: cfg.sv_path;
	fs::path scan_root = sv_rel.empty() ? root_path : (root_path / sv_rel);
	if (!fs::exists(scan_root, ec)) {
		scan_root = root_path;
		sv_rel = ".";
	}

	std::vector<std::string> rtl_files = verilog::verilog_module_scanner::find_synthesizable_files(scan_root);
	std::vector<verilog::discovered_module> discovered = verilog::verilog_module_scanner::discover_modules_in_files(rtl_files);

	std::map<std::string, std::string> mod_to_rel_file;
	std::map<std::string, std::vector<verilog::discovered_instantiation>> mod_children;
	std::set<std::string> instantiated_anywhere;

	for (const auto &dm : discovered) {
		if (!mod_to_rel_file.contains(dm.module_name)) {
			mod_to_rel_file[dm.module_name] = to_rel(dm.source_file);
		}
		auto children = verilog::verilog_module_scanner::discover_instantiations(dm.source_file, rtl_files);
		for (const auto &ch : children) {
			if (ch.module_name != dm.module_name) {
				instantiated_anywhere.insert(ch.module_name);
			}
		}
		mod_children[dm.module_name] = std::move(children);
	}

	// Count testbenches associated with each module
	std::vector<verilog::testbench_entry> tb_entries;
	std::string tb_cfg_path;
	std::string tb_err;
	verilog::fpga_project_settings::load_testbenches(project_root, tb_entries, tb_cfg_path, tb_err);

	std::map<std::string, size_t> mod_tb_counts;
	for (const auto &e : tb_entries) {
		for (const auto &m : e.modules) {
			mod_tb_counts[m]++;
		}
	}

	std::vector<std::string> disk_tbs = verilog::verilog_module_scanner::find_testbench_files(root_path);

	// Determine root module(s) for the tree
	std::vector<std::string> roots;
	if (!args_.module.empty()) {
		std::string resolved = verilog::verilog_module_scanner::resolve_module_name(args_.module, "", rtl_files);
		if (mod_to_rel_file.contains(resolved)) {
			roots.push_back(resolved);
		} else if (mod_to_rel_file.contains(args_.module)) {
			roots.push_back(args_.module);
		} else {
			set_failure(ctx, std::format("Module '{}' not found in RTL sources", args_.module));
			return std::format("Module `{}` not found under `{}`.", args_.module, sv_rel);
		}
	} else if (!cfg.top_module.empty() && mod_to_rel_file.contains(cfg.top_module)) {
		roots.push_back(cfg.top_module);
	} else {
		for (const auto &[mod_name, _] : mod_to_rel_file) {
			if (!instantiated_anywhere.contains(mod_name)) {
				roots.push_back(mod_name);
			}
		}
		if (roots.empty() && !discovered.empty()) {
			roots.push_back(discovered.front().module_name);
		}
	}

	std::ostringstream ss;
	ss << "### Verilog Project Map\n\n";
	if (has_cfg && !cfg.target.empty()) {
		ss << std::format("- **Target**: `{}` | **Device**: `{}` | **Package**: `{}` | **Freq**: {:.1f} MHz\n",
				  cfg.target, cfg.device.empty() ? "auto" : cfg.device,
				  cfg.package.empty() ? "auto" : cfg.package, cfg.freq_mhz);
	} else {
		ss << "- **Target**: *(unconfigured — run `verilog_project_settings` if targeting FPGA PnR)*\n";
	}
	ss << std::format("- **RTL Root (`sv_path`)**: `{}` ({} file(s), {} module(s))\n",
			  sv_rel.empty() ? "." : sv_rel, rtl_files.size(), mod_to_rel_file.size());
	if (!cfg.top_module.empty()) {
		ss << std::format("- **Configured `top_module`**: `{}`\n", cfg.top_module);
	}
	ss << std::format("- **Testbenches**: {} configured in `turbostar.json`, {} file(s) on disk\n\n",
			  tb_entries.size(), disk_tbs.size());

	std::set<std::string> visited_global;
	if (!roots.empty()) {
		ss << "#### Module Hierarchy\n```text\n";
		auto format_node_label = [&](std::string_view mod_name, std::span<const std::string> inst_names) -> std::string {
			auto fit = mod_to_rel_file.find(std::string(mod_name));
			std::string file_str = (fit != mod_to_rel_file.end()) ? fit->second : "?";
			size_t tb_cnt = mod_tb_counts[std::string(mod_name)];
			std::string tb_str = (tb_cnt > 0) ? std::format("tb: {}", tb_cnt) : "no tb";
			if (inst_names.empty()) {
				return std::format("{} ({}) [{}]", mod_name, file_str, tb_str);
			}
			std::string inst_str;
			size_t show_n = std::min(inst_names.size(), size_t(8));
			for (size_t i = 0; i < show_n; ++i) {
				if (i > 0) inst_str += ", ";
				inst_str += inst_names[i];
			}
			if (inst_names.size() > show_n) {
				inst_str += std::format(", +{} more", inst_names.size() - show_n);
			}
			return std::format("{} : {} ({}) [{}]", inst_str, mod_name, file_str, tb_str);
		};

		auto print_tree = [&](auto &self,
				      const std::string &mod_name,
				      std::span<const std::string> inst_names,
				      const std::string &prefix,
				      bool is_last,
				      bool is_root,
				      int depth,
				      std::set<std::string> &path_stack) -> void {
			visited_global.insert(mod_name);
			std::string label = format_node_label(mod_name, inst_names);
			if (is_root) {
				ss << label << "\n";
			} else {
				ss << prefix << (is_last ? "└── " : "├── ") << label << "\n";
			}

			if (depth >= args_.max_depth || path_stack.contains(mod_name)) {
				return;
			}
			auto cit = mod_children.find(mod_name);
			if (cit == mod_children.end() || cit->second.empty()) {
				return;
			}

			// Collapse multiple instantiations of the same leaf module into one line with all instance names;
			// expand non-leaf modules per instance so each subtree stays clear.
			struct child_group {
				std::string mod_name;
				std::vector<std::string> inst_names;
			};
			std::vector<child_group> grouped;
			std::map<std::string, size_t> leaf_pos_map;
			for (const auto &ch : cit->second) {
				if (ch.module_name == mod_name || !mod_to_rel_file.contains(ch.module_name)) {
					continue;
				}
				auto ch_cit = mod_children.find(ch.module_name);
				bool is_leaf = (ch_cit == mod_children.end() || ch_cit->second.empty());
				if (is_leaf) {
					auto pit = leaf_pos_map.find(ch.module_name);
					if (pit == leaf_pos_map.end()) {
						leaf_pos_map[ch.module_name] = grouped.size();
						grouped.push_back({ch.module_name, {ch.instance_name}});
					} else {
						grouped[pit->second].inst_names.push_back(ch.instance_name);
					}
				} else {
					grouped.push_back({ch.module_name, {ch.instance_name}});
				}
			}

			path_stack.insert(mod_name);
			std::string child_prefix = is_root ? "" : (prefix + (is_last ? "    " : "│   "));
			for (size_t i = 0; i < grouped.size(); ++i) {
				bool last_child = (i + 1 == grouped.size());
				self(self, grouped[i].mod_name, grouped[i].inst_names, child_prefix, last_child, false,
				     depth + 1, path_stack);
			}
			path_stack.erase(mod_name);
		};

		for (const auto &r : roots) {
			std::set<std::string> path_stack;
			print_tree(print_tree, r, {}, "", true, true, 0, path_stack);
		}
		ss << "```\n\n";
	}

	// Any RTL modules not reached from roots
	std::vector<std::string> unreached;
	for (const auto &[mod_name, rel_f] : mod_to_rel_file) {
		if (!visited_global.contains(mod_name)) {
			unreached.push_back(std::format("`{}` (`{}`)", mod_name, rel_f));
		}
	}
	if (!unreached.empty()) {
		ss << "#### Uninstantiated / Standalone RTL Modules\n";
		for (const auto &u : unreached) {
			ss << std::format("- {}\n", u);
		}
		ss << "\n";
	}

	// Group RTL and testbench files by directory so subdirectories are immediately visible
	auto append_dir_listing = [&](std::string_view heading, const std::vector<std::string> &files) {
		if (files.empty()) {
			return;
		}
		std::map<std::string, std::vector<std::string>> by_dir;
		for (const auto &f : files) {
			fs::path rel_p(to_rel(f));
			std::string dir = rel_p.has_parent_path() ? rel_p.parent_path().generic_string() : ".";
			by_dir[dir].push_back(rel_p.filename().generic_string());
		}
		ss << std::format("#### {}\n", heading);
		for (auto &[dir, names] : by_dir) {
			std::sort(names.begin(), names.end());
			std::string joined;
			size_t show_n = std::min(names.size(), size_t(12));
			for (size_t i = 0; i < show_n; ++i) {
				if (i > 0) joined += ", ";
				joined += std::format("`{}`", names[i]);
			}
			if (names.size() > show_n) {
				joined += std::format(", *(+{} more)*", names.size() - show_n);
			}
			ss << std::format("- **`{}/`** ({}): {}\n", dir, names.size(), joined);
		}
		ss << "\n";
	};

	append_dir_listing("RTL Source Directories", rtl_files);
	append_dir_listing("Testbench Directories", disk_tbs);

	set_success(ctx);
	return ss.str();
}

} // namespace tools
