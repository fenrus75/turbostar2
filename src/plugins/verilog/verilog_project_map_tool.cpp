#include "plugins/verilog/verilog_project_map_tool.h"
#include "plugins/verilog/fpga_project_settings.h"
#include "plugins/verilog/verilog_module_scanner.h"
#include "mapped_file.h"

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

	std::map<std::string, size_t> mod_line_counts;

	for (const auto &dm : discovered) {
		if (!mod_to_rel_file.contains(dm.module_name)) {
			mod_to_rel_file[dm.module_name] = to_rel(dm.source_file);
			std::string mf_err;
			if (auto mf = mapped_file::get_cached(dm.source_file, mf_err, /*populate=*/false)) {
				mod_line_counts[dm.module_name] = mf->lines().size();
			}
		}
		auto children = verilog::verilog_module_scanner::discover_instantiations(dm.source_file, rtl_files);
		for (const auto &ch : children) {
			if (ch.module_name != dm.module_name) {
				instantiated_anywhere.insert(ch.module_name);
			}
		}
		mod_children[dm.module_name] = std::move(children);
	}

	// Count testbenches by tier and hierarchical defaults per module
	std::vector<verilog::testbench_entry> tb_entries;
	std::string tb_cfg_path;
	std::string tb_err;
	verilog::fpga_project_settings::load_testbenches(project_root, tb_entries, tb_cfg_path, tb_err);

	std::vector<std::string> hier_modules;
	std::string hier_err;
	verilog::fpga_project_settings::load_hierarchical_modules(project_root, hier_modules, hier_err);
	std::set<std::string> hier_set(hier_modules.begin(), hier_modules.end());

	struct tb_tier_counts {
		size_t quick{0};
		size_t comprehensive{0};
		size_t extreme{0};
		size_t total() const noexcept { return quick + comprehensive + extreme; }
	};
	std::map<std::string, tb_tier_counts> mod_tb_tiers;
	std::set<std::string> configured_tb_rels;
	for (const auto &e : tb_entries) {
		configured_tb_rels.insert(to_rel(fs::path(e.testbench).is_absolute() ? fs::path(e.testbench) : (root_path / e.testbench)));
		for (const auto &m : e.modules) {
			auto &tc = mod_tb_tiers[m];
			if (e.level == "extreme") {
				tc.extreme++;
			} else if (e.level == "comprehensive") {
				tc.comprehensive++;
			} else {
				tc.quick++;
			}
		}
	}

	std::vector<std::string> disk_tbs = verilog::verilog_module_scanner::find_testbench_files(root_path);
	std::vector<std::string> unconfigured_tbs;
	for (const auto &dtb : disk_tbs) {
		std::string rel_tb = to_rel(dtb);
		if (!configured_tb_rels.contains(rel_tb)) {
			unconfigured_tbs.push_back(std::move(rel_tb));
		}
	}

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

	if (!roots.empty()) {
		ss << "#### Module Hierarchy\n```text\n";
		auto format_node_label = [&](std::string_view mod_name, std::span<const std::string> inst_names) -> std::string {
			std::string mod_key(mod_name);
			auto fit = mod_to_rel_file.find(mod_key);
			std::string file_str = (fit != mod_to_rel_file.end()) ? fit->second : "?";
			auto lit = mod_line_counts.find(mod_key);
			std::string loc_str = (lit != mod_line_counts.end() && lit->second > 0)
				? std::format("{}, {}L", file_str, lit->second)
				: file_str;

			const auto &tc = mod_tb_tiers[mod_key];
			std::string meta_str;
			if (tc.total() == 0) {
				meta_str = "no tb";
			} else {
				std::string tiers;
				auto add_tier = [&](size_t cnt, char tag) {
					if (cnt == 0) return;
					if (!tiers.empty()) tiers += ", ";
					tiers += std::format("{}{}", cnt, tag);
				};
				add_tier(tc.quick, 'q');
				add_tier(tc.comprehensive, 'c');
				add_tier(tc.extreme, 'e');
				meta_str = std::format("tb: {}", tiers);
			}
			if (hier_set.contains(mod_key)) {
				meta_str += ", hier";
			}

			if (inst_names.empty()) {
				return std::format("{} ({}) [{}]", mod_name, loc_str, meta_str);
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
			return std::format("{} : {} ({}) [{}]", inst_str, mod_name, loc_str, meta_str);
		};

		auto print_tree = [&](auto &self,
				      const std::string &mod_name,
				      std::span<const std::string> inst_names,
				      const std::string &prefix,
				      bool is_last,
				      bool is_root,
				      int depth,
				      std::set<std::string> &path_stack) -> void {
			std::string label = format_node_label(mod_name, inst_names);
			if (is_root) {
				ss << label << "\n";
			} else {
				ss << prefix << (is_last ? "└── " : "├── ") << label << "\n";
			}

			if (path_stack.contains(mod_name)) {
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

			std::string child_prefix = is_root ? "" : (prefix + (is_last ? "    " : "│   "));
			if (depth >= args_.max_depth) {
				if (!grouped.empty()) {
					ss << child_prefix << std::format("└── ... ({} child group(s) hidden by max_depth={})\n",
									    grouped.size(), args_.max_depth);
				}
				return;
			}

			path_stack.insert(mod_name);
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

	// Compute uninstantiated modules across the full project graph (never distorted by max_depth or Module filter)
	if (args_.module.empty()) {
		std::set<std::string> full_reachable;
		auto mark_reachable = [&](auto &self, const std::string &m) -> void {
			if (!full_reachable.insert(m).second) {
				return;
			}
			auto cit = mod_children.find(m);
			if (cit != mod_children.end()) {
				for (const auto &ch : cit->second) {
					if (mod_to_rel_file.contains(ch.module_name)) {
						self(self, ch.module_name);
					}
				}
			}
		};
		for (const auto &r : roots) {
			mark_reachable(mark_reachable, r);
		}

		std::vector<std::string> unreached;
		for (const auto &[mod_name, rel_f] : mod_to_rel_file) {
			if (!full_reachable.contains(mod_name)) {
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
	}

	// Group RTL and testbench files by directory so subdirectories are immediately visible
	auto append_dir_listing = [&](std::string_view heading, const std::vector<std::string> &files, size_t max_per_dir) {
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
			size_t show_n = std::min(names.size(), max_per_dir);
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

	append_dir_listing("RTL Source Directories", rtl_files, 30);
	append_dir_listing("Testbench Directories", disk_tbs, 12);
	if (!unconfigured_tbs.empty()) {
		std::sort(unconfigured_tbs.begin(), unconfigured_tbs.end());
		std::string joined;
		size_t show_n = std::min(unconfigured_tbs.size(), size_t(12));
		for (size_t i = 0; i < show_n; ++i) {
			if (i > 0) joined += ", ";
			joined += std::format("`{}`", unconfigured_tbs[i]);
		}
		if (unconfigured_tbs.size() > show_n) {
			joined += std::format(", *(+{} more)*", unconfigured_tbs.size() - show_n);
		}
		ss << std::format("- **Unconfigured Testbenches** ({}): {}\n\n", unconfigured_tbs.size(), joined);
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
