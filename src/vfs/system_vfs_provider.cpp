#include "vfs/system_vfs_provider.h"
#include "agentlib/ai_agent.h"
#include "agentlib/skill_manager.h"
#include "agentlib/subagent_manager.h"
#include "agentlib/tool_registry.h"
#include "build_error_manager.h"
#include "git_manager.h"
#include "project_manager.h"
#include "mcp/mcp_manager.h"
#include "system_docs_embedded.h"
#include "fs_utils.h"
#include "config_manager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;

namespace turbostar {

static bool contains_case_insensitive(const std::string &haystack, const std::string &needle)
{
	if (needle.empty()) return true;
	auto it = std::search(
		haystack.begin(), haystack.end(),
		needle.begin(), needle.end(),
		[](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
	);
	return it != haystack.end();
}

static std::string get_family_reason_helper(const std::string &fam)
{
	std::string reason = agentlib::tool_registry::get_instance().get_tool_family_reason(fam);
	if (reason.empty()) {
		std::string cached = config_manager::get_instance().get_mcp_server_when_to_activate(fam, false);
		if (cached.empty()) {
			cached = config_manager::get_instance().get_mcp_server_when_to_activate(fam, true);
		}
		if (!cached.empty()) {
			reason = cached;
		} else if (fam == "base") {
			reason = "Always active core system tools.";
		} else {
			reason = std::format("Activate when needing tools from the {} family.", fam);
		}
	}
	return reason;
}

static std::vector<std::string> extract_tool_families(const std::string &family_str)
{
	std::vector<std::string> result;
	std::stringstream ss(family_str);
	std::string item;
	while (std::getline(ss, item, ',')) {
		std::stringstream ss2(item);
		std::string item2;
		while (std::getline(ss2, item2, '|')) {
			while (!item2.empty() && (item2.front() == ' ' || item2.front() == '\t')) item2.erase(item2.begin());
			while (!item2.empty() && (item2.back() == ' ' || item2.back() == '\t')) item2.pop_back();
			if (!item2.empty()) result.push_back(item2);
		}
	}
	return result;
}

static std::string generate_tool_families_index(const std::string &query)
{
	std::stringstream ss;
	ss << "# Tool Families Overview\n\n";
	ss << "> [!TIP]\n";
	ss << "> - Tools in TurboStar are organized into tool families. `base` tools are always active.\n";
	ss << "> - If a tool belongs to an inactive family, you must call `activate_tool_family(family_name=\"<family>\")` before executing it.\n";
	ss << "> - To inspect member tools and activation guidance for a specific family, read `system://tool-families/<family>.md`.\n\n";

	ss << "| Tool Family | Default Status | Activation Reason | Family Documentation |\n";
	ss << "| ----------- | -------------- | ----------------- | -------------------- |\n";

	auto families = agentlib::tool_registry::get_instance().get_all_registered_families();
	std::sort(families.begin(), families.end());

	for (const auto &fam : families) {
		if (fam.starts_with(':')) continue;

		std::string reason = get_family_reason_helper(fam);
		if (!query.empty()) {
			if (!contains_case_insensitive(fam, query) && !contains_case_insensitive(reason, query)) {
				continue;
			}
		}

		std::string status_str = (fam == "base") ? "**Active**" : "*Inactive*";
		std::string link = std::format("[system://tool-families/{}.md](system://tool-families/{}.md)", fam, fam);
		ss << std::format("| `{}` | {} | {} | {} |\n", fam, status_str, reason, link);
	}

	return ss.str();
}

static std::string generate_tool_family_detail(const std::string &fam_name)
{
	std::string clean_name = fam_name;
	if (clean_name.ends_with(".md")) {
		clean_name = clean_name.substr(0, clean_name.length() - 3);
	}

	std::stringstream ss;
	ss << std::format("# Tool Family: `{}`\n\n", clean_name);

	std::string reason = get_family_reason_helper(clean_name);
	ss << std::format("- **Activation Reason**: {}\n", reason);
	if (clean_name != "base") {
		ss << std::format("- **Activation Status**: *Inactive* (Run `activate_tool_family(family_name=\"{}\")` to enable)\n", clean_name);
		ss << std::format("- **Activation Command**: `activate_tool_family(family_name=\"{}\")`\n", clean_name);
	} else {
		ss << "- **Activation Status**: **Active** (Always available core system tools)\n";
	}

	std::string guidance = agentlib::tool_registry::get_instance().get_tool_family_guidance(clean_name);
	if (!guidance.empty()) {
		ss << "\n## Guidance & Overview\n" << guidance << "\n";
	}

	auto all_validators = agentlib::tool_registry::get_instance().get_all_registered_validators();
	std::set<std::string> shared_families;
	int tool_count = 0;

	std::stringstream tools_ss;
	tools_ss << "\n## Member Tools in `" << clean_name << "` Family\n\n";
	tools_ss << "| Tool Name | Description | Detailed Schema Link |\n";
	tools_ss << "| --------- | ----------- | -------------------- |\n";

	for (const auto &val : all_validators) {
		if (!val) continue;
		std::string fam_str = val->get_family();
		auto val_fams = extract_tool_families(fam_str);

		bool belongs = false;
		for (const auto &f : val_fams) {
			if (f == clean_name) {
				belongs = true;
			} else {
				shared_families.insert(f);
			}
		}

		if (belongs) {
			tool_count++;
			std::string tname = val->get_name();
			std::string tdesc = val->get_description();
			while (!tdesc.empty() && (tdesc.front() == '\n' || tdesc.front() == '\r')) {
				tdesc.erase(tdesc.begin());
			}
			size_t first_nl = tdesc.find('\n');
			if (first_nl != std::string::npos) {
				tdesc = tdesc.substr(0, first_nl);
			}
			std::replace(tdesc.begin(), tdesc.end(), '|', ',');

			std::string link = std::format("[system://tools_detailed.md?search={}](system://tools_detailed.md?search={})", tname, tname);
			tools_ss << std::format("| `{}` | {} | {} |\n", tname, tdesc, link);
		}
	}

	if (!shared_families.empty() && tool_count > 0) {
		ss << "- **Related / Shared Families**: ";
		bool first = true;
		for (const auto &sf : shared_families) {
			if (!first) ss << ", ";
			ss << std::format("[{}](system://tool-families/{}.md)", sf, sf);
			first = false;
		}
		ss << "\n";
	}

	if (tool_count == 0) {
		tools_ss << "*No static tools explicitly declared under family '" << clean_name << "'.*\n";
	}

	ss << tools_ss.str();

	return ss.str();
}

static std::string generate_subagents_index(const std::string &query)
{
	auto agents = agentlib::ai_agent::get_all_active_agents();
	std::stringstream ss;
	ss << "# Active & Recent Subagents\n\n";
	ss << "| ID | Name / Role | Status | Model | Task Summary |\n";
	ss << "| --- | ----------- | ------ | ----- | ------------ |\n";

	int count = 0;
	for (const auto &agent : agents) {
		if (!agent) continue;
		int id = agent->get_id();
		std::string name = agent->get_name();
		std::string status_str = agentlib::agent_status_to_string(agent->get_status());
		std::string model_name = agent->get_model() ? agent->get_model()->get_name() : "default";
		std::string task = agent->get_task_description();

		if (!query.empty() && !contains_case_insensitive(std::to_string(id) + " " + name + " " + task + " " + status_str, query)) {
			continue;
		}

		std::string short_task = task.length() > 60 ? (task.substr(0, 57) + "...") : task;
		ss << "| [`" << id << "`](system://subagents/" << id << ".md) | `" << name << "` | `"
		   << status_str << "` | " << model_name << " | " << short_task << " |\n";
		count++;
	}

	if (count == 0) {
		if (!query.empty()) {
			return "No subagents match the query.";
		}
		return "No active or recent subagents found.";
	}

	return ss.str();
}

static std::string generate_subagent_summary(int id)
{
	auto agent = agentlib::ai_agent::find_agent_by_id(id);
	if (!agent) {
		return "Subagent ID " + std::to_string(id) + " was not found or has exited.";
	}

	std::stringstream ss;
	ss << "# Subagent " << id << " (" << agent->get_name() << ")\n\n";
	ss << "- **Subagent ID**: `" << id << "`\n";
	ss << "- **Role / Profile**: `" << agent->get_name() << "`\n";
	ss << "- **Status**: `" << agentlib::agent_status_to_string(agent->get_status()) << "`\n";
	ss << "- **Model**: `" << (agent->get_model() ? agent->get_model()->get_name() : "default") << "`\n";
	if (agent->has_final_result()) {
		ss << "- **Has Final Result**: Yes (`system://subagents/" << id << "/final_result.md`)\n\n";
	} else {
		ss << "- **Has Final Result**: No\n\n";
	}

	ss << "## Target Task\n";
	ss << (agent->get_task_description().empty() ? "(none specified)" : agent->get_task_description()) << "\n\n";

	if (agent->has_final_result()) {
		ss << "## Final Result Output Summary\n";
		std::string res = agent->get_final_result();
		if (res.length() > 300) {
			ss << res.substr(0, 300) << "...\n*(Read [final_result.md](system://subagents/" << id << "/final_result.md) for full result)*\n\n";
		} else {
			ss << res << "\n\n";
		}
	}

	ss << "## Navigation & Detailed Logs\n";
	ss << "- [Final Result Output](system://subagents/" << id << "/final_result.md)\n";
	ss << "- [Full Execution Transcript](system://subagents/" << id << "/transcript.md)\n";

	return ss.str();
}

static std::string generate_subagent_final_result(int id)
{
	auto agent = agentlib::ai_agent::find_agent_by_id(id);
	if (!agent) {
		return "Subagent ID " + std::to_string(id) + " was not found or has exited.";
	}

	if (!agent->has_final_result()) {
		return "Subagent " + std::to_string(id) + " (" + agent->get_name() + ") has not reported a final result yet.";
	}

	std::stringstream ss;
	ss << "# Final Result for Subagent " << id << " (" << agent->get_name() << ")\n\n";
	ss << agent->get_final_result() << "\n";
	return ss.str();
}

static std::string generate_subagent_transcript(int id, const std::string &query)
{
	auto agent = agentlib::ai_agent::find_agent_by_id(id);
	if (!agent) {
		return "Subagent ID " + std::to_string(id) + " was not found or has exited.";
	}

	auto interactions = agent->get_interactions();
	std::stringstream ss;
	ss << "# Execution Transcript for Subagent " << id << " (" << agent->get_name() << ")\n\n";

	if (interactions.empty()) {
		ss << "No interaction turns recorded yet for subagent " << id << ".\n";
		return ss.str();
	}

	int tail_count = 0;
	if (query.starts_with("tail=")) {
		try {
			tail_count = std::stoi(query.substr(5));
		} catch (...) {
			tail_count = 0;
		}
	}

	size_t start_idx = 0;
	if (tail_count > 0 && static_cast<size_t>(tail_count) < interactions.size()) {
		start_idx = interactions.size() - static_cast<size_t>(tail_count);
	}

	for (size_t i = start_idx; i < interactions.size(); ++i) {
		const auto &inter = interactions[i];
		std::string raw = inter ? inter->get_raw_text() : "";
		if (!query.empty() && !query.starts_with("tail=") && !contains_case_insensitive(raw, query)) {
			continue;
		}
		ss << "### Turn " << (i + 1) << "\n";
		ss << raw << "\n\n";
	}

	return ss.str();
}

system_vfs_provider::system_vfs_provider()
{
	// 1. Dynamic Generator: system://agents.md
	register_generator("agents.md", [](const std::string &) -> std::string {
		auto &sm = agentlib::subagent_manager::get_instance();
		auto agents = sm.get_subagents();
		std::string out = "# Available Subagents\n\n";
		out += "| Subagent Name | Description |\n";
		out += "| :--- | :--- |\n";
		for (const auto &sa : agents) {
			out += "| `" + sa.name + "` | " + sa.description + " |\n";
		}
		return out;
	});

	// 2. Dynamic Generator: system://tools.md (Summary Table)
	register_generator("tools.md", [](const std::string &query) -> std::string {
		auto tools_json = agentlib::tool_registry::get_instance().get_tools_json();
		std::string search = query;
		if (search.starts_with("search=")) {
			search = search.substr(7);
		}
		std::string search_lower = search;
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);

		std::ostringstream oss;
		oss << "# Registered System Tools\n\n";
		oss << "> [!TIP]\n";
		oss << "> - For complete parameter schemas (types, descriptions, required fields), read [`system://tools_detailed.md`](system://tools_detailed.md).\n";
		oss << "> - To filter tools by name or keyword, append `?search=<pattern>` (e.g., `fs_read_lines(\"system://tools.md?search=git\")` or `fs_read_lines(\"system://tools_detailed.md?search=git\")`).\n\n";
		oss << "| Tool Name | Description |\n";
		oss << "| :--- | :--- |\n";

		for (const auto &tool_node : tools_json) {
			if (tool_node.contains("function")) {
				auto func = tool_node["function"];
				std::string name = func.value("name", "unknown");
				std::string desc = func.value("description", "");

				if (!search_lower.empty()) {
					std::string name_lower = name;
					std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
					if (name_lower.find(search_lower) == std::string::npos) {
						continue;
					}
				}

				for (char &c : desc) {
					if (c == '\n' || c == '\r') c = ' ';
					else if (c == '|') c = '/';
				}

				oss << "| `" << name << "` | " << desc << " |\n";
			}
		}
		return oss.str();
	});

	// 3. Dynamic Generator: system://tools_detailed.md (Full Parameter Schema Inspection)
	register_generator("tools_detailed.md", [](const std::string &query) -> std::string {
		auto tools_json = agentlib::tool_registry::get_instance().get_tools_json();
		std::string search = query;
		if (search.starts_with("search=")) {
			search = search.substr(7);
		}
		std::string search_lower = search;
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);

		std::ostringstream oss;
		oss << "# Detailed System Tool Schemas\n\n";
		oss << "> [!TIP]\n";
		oss << "> - To filter tool schemas by name or keyword, append `?search=<pattern>` (e.g., `fs_read_lines(\"system://tools_detailed.md?search=git\")`).\n\n";

		for (const auto &tool_node : tools_json) {
			if (tool_node.contains("function")) {
				auto func = tool_node["function"];
				std::string name = func.value("name", "unknown");
				std::string desc = func.value("description", "");

				if (!search_lower.empty()) {
					std::string name_lower = name;
					std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
					if (name_lower.find(search_lower) == std::string::npos) {
						continue;
					}
				}

				for (char &c : desc) {
					if (c == '\n' || c == '\r') c = ' ';
				}

				oss << "### `" << name << "`\n";
				oss << "* **Description:** " << desc << "\n";
				if (func.contains("parameters") && func["parameters"].contains("properties") && func["parameters"]["properties"].is_object()) {
					oss << "* **Arguments:**\n";
					auto props = func["parameters"]["properties"];
					nlohmann::json req_array = nlohmann::json::array();
					if (func["parameters"].contains("required") && func["parameters"]["required"].is_array()) {
						req_array = func["parameters"]["required"];
					}
					for (auto it = props.begin(); it != props.end(); ++it) {
						std::string param_name = it.key();
						auto param_info = it.value();
						std::string p_type = param_info.value("type", "unknown");
						std::string p_desc = param_info.value("description", "");
						bool is_required = false;
						for (const auto &req_item : req_array) {
							if (req_item == param_name) {
								is_required = true;
								break;
							}
						}
						std::string req_str = is_required ? "required" : "optional";
						oss << "    * `" << param_name << "` *(" << p_type << ", " << req_str << ")*: " << p_desc << "\n";
					}
				} else {
					oss << "* **Arguments:** None\n";
				}
				oss << "\n";
			}
		}
		return oss.str();
	});

	// 4. Dynamic Generator: system://mcp.md
	register_generator("mcp.md", [](const std::string &) -> std::string {
		auto &mcp = agentlib::mcp_manager::get_instance();
		auto servers = mcp.get_servers();
		std::string out = "# Configured MCP Servers\n\n";
		out += "| Server Name | Status |\n";
		out += "| :--- | :--- |\n";
		for (const auto &s : servers) {
			if (s) {
				out += "| `" + s->get_name() + "` | " + (s->is_enabled() ? "Enabled" : "Disabled") + " |\n";
			}
		}
		return out;
	});

	// 5. Dynamic Generator: system://skills.md
	register_generator("skills.md", [](const std::string &query) -> std::string {
		auto &skills = agentlib::skill_manager::get_instance().get_skills();
		std::stringstream ss;
		ss << "# Available Agent Skills\n\n";
		ss << "| Skill Name | URI | Description |\n";
		ss << "| ---------- | --- | ----------- |\n";
		int count = 0;
		for (const auto &s : skills) {
			if (s.visible) {
				if (!query.empty() && !contains_case_insensitive(s.name + " " + s.description, query)) {
					continue;
				}
				ss << "| `" << s.name << "` | `" << s.uri << "` | " << s.description << " |\n";
				count++;
			}
		}
		if (count == 0) {
			if (!query.empty()) {
				return "No skills match the query.";
			}
			return "No skills are currently available.";
		}
		return ss.str();
	});

	// 6. Dynamic Generator: system://project/diagnostics.md
	register_generator("project/diagnostics.md", [](const std::string &query) -> std::string {
		struct file_stats {
			int compiler_errors = 0;
			int compiler_warnings = 0;
			int lsp_errors = 0;
			int lsp_warnings = 0;
		};
		std::map<std::string, file_stats> workspace_stats;
		std::set<std::string> unique_paths;

		const auto &build_errors = build_error_manager::get_instance().get_errors();
		for (const auto &err : build_errors) {
			if (!err.filepath.empty()) {
				unique_paths.insert(err.filepath);
			}
		}

		for (const auto &raw_path : unique_paths) {
			file_stats stats;
			bool has_issues = false;

			for (const auto &err : build_errors) {
				if (err.filepath == raw_path) {
					if (err.is_warning)
						stats.compiler_warnings++;
					else
						stats.compiler_errors++;
					has_issues = true;
				}
			}

			if (has_issues) {
				if (!query.empty() && !contains_case_insensitive(raw_path, query)) {
					continue;
				}
				workspace_stats[raw_path] = stats;
			}
		}

		if (workspace_stats.empty()) {
			return "No compilation errors or warnings found.";
		}

		std::stringstream ss;
		ss << "# Workspace Compilation Diagnostics\n\n";
		ss << "| File | Compiler Errors | Compiler Warnings | LSP Errors | LSP Warnings |\n";
		ss << "| ---- | --------------- | ----------------- | ---------- | ------------ |\n";

		int total_ce = 0, total_cw = 0, total_le = 0, total_lw = 0;
		for (const auto &[file, stats] : workspace_stats) {
			ss << "| `" << file << "` | " << stats.compiler_errors << " | " << stats.compiler_warnings
			   << " | " << stats.lsp_errors << " | " << stats.lsp_warnings << " |\n";
			total_ce += stats.compiler_errors;
			total_cw += stats.compiler_warnings;
			total_le += stats.lsp_errors;
			total_lw += stats.lsp_warnings;
		}

		ss << "| **Total** | **" << total_ce << "** | **" << total_cw << "** | **" << total_le
		   << "** | **" << total_lw << "** |\n";

		return ss.str();
	});

	// 7. Dynamic Generator: system://project/info.md
	register_generator("project/info.md", [](const std::string &) -> std::string {
		std::string proj_root = project_manager::get_instance().get_project_root();
		std::stringstream ss;
		ss << "# Project Workspace Overview\n\n";
		ss << "- **Project Root**: `" << (proj_root.empty() ? "(none)" : proj_root) << "`\n";
		
		std::string remote_url = git_manager::get_instance().get_remote_origin_url();
		ss << "- **Upstream Repository**: " << (remote_url.empty() ? "`none`" : "`" + remote_url + "`") << "\n";

		std::string branch = git_manager::get_instance().get_current_branch();
		ss << "- **Git Branch**: " << (branch.empty() ? "`none`" : "`" + branch + "`") << "\n";

		std::string build_system = "Unknown / Custom";
		if (fs::exists(fs::path(proj_root) / "meson.build")) {
			build_system = "Meson";
		} else if (fs::exists(fs::path(proj_root) / "CMakeLists.txt")) {
			build_system = "CMake";
		} else if (fs::exists(fs::path(proj_root) / "Makefile")) {
			build_system = "Make";
		}
		ss << "- **Build System**: " << build_system << "\n";
		ss << "- **GEMINI.md Presence**: " << (fs::exists(fs::path(proj_root) / "GEMINI.md") ? "Yes" : "No") << "\n";
		ss << "- **AGENTS.md Presence**: " << (fs::exists(fs::path(proj_root) / "AGENTS.md") ? "Yes" : "No") << "\n";
		return ss.str();
	});

	register_generator("subagents.md", [](const std::string &query) -> std::string {
		return generate_subagents_index(query);
	});

	// Register file purpose descriptions ("when to read this file")
	register_description("languages/cpp23.md", "Read when writing or refactoring C++23 code.");
	register_description("languages/c17.md", "Read when writing or refactoring C17 code.");
	register_description("languages/python311.md", "Read when writing or refactoring Python code.");
	register_description("languages/rust2021.md", "Read when writing or refactoring Rust 2021 code.");
	register_description("languages/typescript.md", "Read when writing or refactoring TypeScript/JavaScript code.");
	register_description("languages/verilog.md", "Read when writing or refactoring Verilog/SystemVerilog code.");

	register_description("workflows/code_review.md", "Read before conducting multi-file code reviews, performing file slicing, or managing review item checklists.");
	register_description("workflows/plan_mode.md", "Read when entering plan mode for complex multi-file tasks, executing read-only exploration, or forming plans.");
	register_description("workflows/crash_analysis.md", "Read when investigating crash reports, core dumps, log tracebacks, or test failures via What-How-Where protocol.");

	register_description("agents.md", "Read to discover available subagent profiles and their specialization roles before invoking subagents.");
	register_description("tools.md", "Read to discover available system tools and concise usage descriptions (supports ?search=<query>).");
	register_description("tools_detailed.md", "Read for full parameter types, descriptions, and schemas for system tools (supports ?search=<query>).");
	register_description("tool-families.md", "Read to discover available tool families, activation criteria, and member tools.");
	register_description("mcp.md", "Read to check active Model Context Protocol (MCP) server connections, transport types, and status.");
	register_description("skills.md", "Read to discover available specialized agent skills and their instruction URIs (supports ?search=<query>).");
	register_description("project/diagnostics.md", "Read to check summary of active compiler errors, warnings, and LSP diagnostics (supports ?search=<query>).");
	register_description("project/info.md", "Read to check project workspace root, build system type, and instruction file presence.");
	register_description("subagents.md", "Read to check summary index of all active and recent subagents.");
	register_description("subagents", "Directory containing active subagent dashboards, final results, and execution transcripts.");
	// Register directory purpose descriptions
	register_description("languages", "Directory containing language-specific development guidelines and standards.");
	register_description("workflows", "Directory containing subagent and system workflow guidelines.");
	register_description("tool-families", "Directory containing tool family activation guides and member tool specifications.");
	register_description("project", "Directory containing project-level build status, diagnostics, and workspace information.");

	register_generator("tool-families.md", [](const std::string &query) {
		return generate_tool_families_index(query);
	});
	register_generator("tool-families/base.md", [](const std::string &query) {
		return generate_tool_family_detail("base");
	});
}

std::string system_vfs_provider::resolve_path(const std::string &uri, std::string *out_query) const
{
	std::string path = uri;

	// Strip scheme
	if (path.starts_with("system://")) {
		path = path.substr(9);
	} else if (path.starts_with("system:")) {
		path = path.substr(7);
	}

	// Strip leading slashes
	while (path.starts_with("/")) {
		path = path.substr(1);
	}

	// Extract query string if present (e.g. system://tools.md?search=git)
	size_t qpos = path.find('?');
	if (qpos != std::string::npos) {
		if (out_query) {
			*out_query = path.substr(qpos + 1);
		}
		path = path.substr(0, qpos);
	} else if (out_query) {
		out_query->clear();
	}

	// Fallback alias resolution for root accesses
	if (path == "cpp23.md" || path == "cpp.md") {
		return "languages/cpp23.md";
	}
	if (path == "c17.md" || path == "c.md") {
		return "languages/c17.md";
	}
	if (path == "python311.md" || path == "python.md" || path == "py.md") {
		return "languages/python311.md";
	}
	if (path == "rust2021.md" || path == "rust.md" || path == "rs.md") {
		return "languages/rust2021.md";
	}
	if (path == "typescript.md" || path == "ts.md" || path == "js.md") {
		return "languages/typescript.md";
	}
	if (path == "verilog.md" || path == "v.md" || path == "sv.md") {
		return "languages/verilog.md";
	}
	if (path == "code_review.md") {
		return "workflows/code_review.md";
	}
	if (path == "plan_mode.md") {
		return "workflows/plan_mode.md";
	}
	if (path == "crash_analysis.md") {
		return "workflows/crash_analysis.md";
	}
	if (path == "tools_detailed.md" || path == "tools/details.md" || path == "tools_detail.md") {
		return "tools_detailed.md";
	}
	if (path == "skills" || path == "skill_list.md" || path == "skills.md") {
		return "skills.md";
	}
	if (path == "diagnostics" || path == "diagnostics.md" || path == "compile_summary.md" || path == "compile-summary.md" || path == "build/summary.md" || path == "project/summary.md" || path == "project/diagnostics" || path == "project/diagnostics.md") {
		return "project/diagnostics.md";
	}
	if (path == "project/info" || path == "project/info.md" || path == "project/overview" || path == "project/overview.md" || path == "project_info.md") {
		return "project/info.md";
	}
	if (path == "subagents" || path == "subagents.md" || path == "subagents/index.md" || path == "subagents_list.md") {
		return "subagents.md";
	}
	if (path.starts_with("subagents/")) {
		std::string sub = path.substr(10);
		if (sub.empty()) {
			return "subagents/";
		}
		size_t slash = sub.find('/');
		if (slash == std::string::npos) {
			if (sub.ends_with(".md")) {
				return "subagents/" + sub;
			} else {
				return "subagents/" + sub + ".md";
			}
		} else {
			std::string id_part = sub.substr(0, slash);
			std::string file_part = sub.substr(slash + 1);
			if (id_part.ends_with(".md")) {
				id_part = id_part.substr(0, id_part.length() - 3);
			}
			if (file_part == "final_result" || file_part == "final_result.md" || file_part == "result" || file_part == "result.md") {
				return "subagents/" + id_part + "/final_result.md";
			}
			if (file_part == "transcript" || file_part == "transcript.md" || file_part == "history" || file_part == "history.md" || file_part == "log.md") {
				return "subagents/" + id_part + "/transcript.md";
			}
			if (file_part == "summary" || file_part == "summary.md" || file_part == "info" || file_part == "info.md") {
				return "subagents/" + id_part + ".md";
			}
			return "subagents/" + id_part + "/" + file_part;
		}
	}
	if (path == "tool-families" || path == "tool-families.md" || path == "tool_families" || path == "tool_families.md") {
		return "tool-families.md";
	}
	if (path.starts_with("tool-families/") || path.starts_with("tool_families/")) {
		std::string sub = path.substr(path.find('/') + 1);
		if (sub.empty()) {
			return "tool-families/";
		}
		if (!sub.ends_with(".md")) {
			sub += ".md";
		}
		return "tool-families/" + sub;
	}

	return path;
}

void system_vfs_provider::register_generator(const std::string &path, vfs_generator_fn generator)
{
	std::lock_guard<std::mutex> lock(generators_mutex_);
	std::string resolved = resolve_path(path);
	generators_[resolved] = std::move(generator);
}

void system_vfs_provider::register_description(const std::string &path, std::string description)
{
	std::lock_guard<std::mutex> lock(generators_mutex_);
	std::string resolved = resolve_path(path);
	descriptions_[resolved] = std::move(description);
}

bool system_vfs_provider::exists(const std::string &uri) const
{
	std::string path = resolve_path(uri);
	while (path.ends_with("/")) {
		path = path.substr(0, path.length() - 1);
	}

	if (path.empty() || path == "tool-families" || path == "subagents" || path == "subagents.md") {
		return true;
	}

	if (path.starts_with("subagents/")) {
		return true;
	}

	if (path.starts_with("tool-families/")) {
		std::string fam = path.substr(14);
		if (fam.ends_with(".md")) {
			fam = fam.substr(0, fam.length() - 3);
		}
		auto families = agentlib::tool_registry::get_instance().get_all_registered_families();
		return std::find(families.begin(), families.end(), fam) != families.end() || fam == "base";
	}

	{
		std::lock_guard<std::mutex> lock(generators_mutex_);
		if (generators_.find(path) != generators_.end()) {
			return true;
		}
		std::string dir_prefix = path + "/";
		for (const auto &[gpath, fn] : generators_) {
			if (gpath.starts_with(dir_prefix)) {
				return true;
			}
		}
	}

	const auto &docs = get_embedded_system_docs();
	if (docs.find(path) != docs.end()) {
		return true;
	}
	std::string dir_prefix = path + "/";
	for (const auto &[dpath, content] : docs) {
		if (dpath.starts_with(dir_prefix)) {
			return true;
		}
	}

	return false;
}

std::optional<agentlib::vfs_file_handle> system_vfs_provider::read_file(const std::string &uri)
{
	std::string query;
	std::string path = resolve_path(uri, &query);

	if (path == "tool-families.md") {
		std::string content = generate_tool_families_index(query);
		return std::make_shared<agentlib::string_content_buffer>(std::move(content));
	}
	if (path.starts_with("tool-families/")) {
		std::string fam_name = path.substr(14);
		std::string content = generate_tool_family_detail(fam_name);
		return std::make_shared<agentlib::string_content_buffer>(std::move(content));
	}
	if (path.starts_with("subagents/")) {
		std::string sub = path.substr(10);
		size_t slash = sub.find('/');
		if (slash != std::string::npos) {
			std::string id_str = sub.substr(0, slash);
			std::string target_file = sub.substr(slash + 1);
			try {
				int id = std::stoi(id_str);
				if (target_file == "final_result.md") {
					std::string content = generate_subagent_final_result(id);
					return std::make_shared<agentlib::string_content_buffer>(std::move(content));
				}
				if (target_file == "transcript.md") {
					std::string content = generate_subagent_transcript(id, query);
					return std::make_shared<agentlib::string_content_buffer>(std::move(content));
				}
			} catch (...) {}
		} else if (sub.ends_with(".md")) {
			std::string id_str = sub.substr(0, sub.length() - 3);
			try {
				int id = std::stoi(id_str);
				std::string content = generate_subagent_summary(id);
				return std::make_shared<agentlib::string_content_buffer>(std::move(content));
			} catch (...) {}
		}
	}

	// Check dynamic generators first
	{
		std::lock_guard<std::mutex> lock(generators_mutex_);
		auto it = generators_.find(path);
		if (it != generators_.end() && it->second) {
			std::string gen_content = it->second(query);
			return std::make_shared<agentlib::string_content_buffer>(std::move(gen_content));
		}
	}

	// Check static embedded docs
	const auto &docs = get_embedded_system_docs();
	auto it = docs.find(path);
	if (it == docs.end()) {
		return std::nullopt;
	}

	std::string content = std::string(it->second);

	// Append custom project/user override if available on disk (e.g. .turbostar/docs/languages/cpp23.md)
	std::string proj_root = fs_utils::get_project_cache_root();
	if (!proj_root.empty()) {
		fs::path override_path = fs::path(proj_root) / "docs" / path;
		if (fs::exists(override_path)) {
			std::ifstream in(override_path);
			if (in.is_open()) {
				std::string override_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				if (!override_text.empty()) {
					content += "\n\n*** PROJECT OVERRIDES (" + override_path.filename().string() + ") ***\n" + override_text;
				}
			}
		}
	}

	return std::make_shared<agentlib::string_content_buffer>(std::move(content));
}

std::optional<agentlib::vfs_file_info> system_vfs_provider::get_file_info(const std::string &uri) const
{
	if (!exists(uri)) {
		return std::nullopt;
	}

	std::string path = resolve_path(uri);
	std::string clean_path = path;
	while (clean_path.ends_with("/")) {
		clean_path = clean_path.substr(0, clean_path.length() - 1);
	}

	if (clean_path == "tool-families") {
		agentlib::vfs_file_info info;
		info.uri = "system://tool-families/";
		info.type = 'D';
		info.size = 0;
		info.size_in_lines = 0;
		info.details = "Directory containing tool family activation guides and member tool specifications.";
		return info;
	}
	if (clean_path == "tool-families.md") {
		std::string content = generate_tool_families_index("");
		agentlib::vfs_file_info info;
		info.uri = "system://tool-families.md";
		info.type = 'F';
		info.size = content.size();
		info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
		info.details = "Read to discover available tool families, activation criteria, and member tools.";
		return info;
	}
	if (clean_path.starts_with("tool-families/")) {
		std::string fam_name = clean_path.substr(14);
		std::string content = generate_tool_family_detail(fam_name);
		agentlib::vfs_file_info info;
		info.uri = "system://tool-families/" + fam_name;
		if (!info.uri.ends_with(".md")) info.uri += ".md";
		info.type = 'F';
		info.size = content.size();
		info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
		if (fam_name.ends_with(".md")) fam_name = fam_name.substr(0, fam_name.length() - 3);
		info.details = get_family_reason_helper(fam_name);
		return info;
	}

	if (clean_path == "subagents") {
		agentlib::vfs_file_info info;
		info.uri = "system://subagents/";
		info.type = 'D';
		info.details = "Directory containing active subagent dashboards, final results, and execution transcripts.";
		return info;
	}
	if (clean_path == "subagents.md") {
		std::string content = generate_subagents_index("");
		agentlib::vfs_file_info info;
		info.uri = "system://subagents.md";
		info.type = 'F';
		info.size = content.size();
		info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
		info.details = "Read to check summary index of all active and recent subagents.";
		return info;
	}
	if (clean_path.starts_with("subagents/")) {
		std::string sub = clean_path.substr(10);
		if (sub.ends_with("/")) sub = sub.substr(0, sub.length() - 1);
		size_t slash = sub.find('/');
		if (slash != std::string::npos) {
			std::string id_str = sub.substr(0, slash);
			std::string file_str = sub.substr(slash + 1);
			try {
				int id = std::stoi(id_str);
				if (file_str == "final_result.md") {
					std::string content = generate_subagent_final_result(id);
					agentlib::vfs_file_info info;
					info.uri = "system://subagents/" + std::to_string(id) + "/final_result.md";
					info.type = 'F';
					info.size = content.size();
					info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
					info.details = "Output of report_final_result tool call for subagent " + std::to_string(id);
					return info;
				}
				if (file_str == "transcript.md") {
					std::string content = generate_subagent_transcript(id, "");
					agentlib::vfs_file_info info;
					info.uri = "system://subagents/" + std::to_string(id) + "/transcript.md";
					info.type = 'F';
					info.size = content.size();
					info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
					info.details = "Step-by-step execution transcript for subagent " + std::to_string(id);
					return info;
				}
			} catch (...) {}
		} else if (sub.ends_with(".md")) {
			std::string id_str = sub.substr(0, sub.length() - 3);
			try {
				int id = std::stoi(id_str);
				std::string content = generate_subagent_summary(id);
				agentlib::vfs_file_info info;
				info.uri = "system://subagents/" + std::to_string(id) + ".md";
				info.type = 'F';
				info.size = content.size();
				info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
				info.details = "Status dashboard & summary for subagent " + std::to_string(id);
				return info;
			} catch (...) {}
		} else {
			try {
				int id = std::stoi(sub);
				agentlib::vfs_file_info info;
				info.uri = "system://subagents/" + std::to_string(id) + "/";
				info.type = 'D';
				info.details = "Directory for subagent " + std::to_string(id);
				return info;
			} catch (...) {}
		}
	}

	std::string dir_prefix = clean_path.empty() ? "" : (clean_path + "/");
	bool is_directory = clean_path.empty();

	if (!is_directory) {
		std::lock_guard<std::mutex> lock(generators_mutex_);
		for (const auto &[gpath, fn] : generators_) {
			if (gpath.starts_with(dir_prefix) && gpath != clean_path) {
				is_directory = true;
				break;
			}
		}
	}

	if (!is_directory) {
		const auto &docs = get_embedded_system_docs();
		for (const auto &[dpath, content] : docs) {
			if (dpath.starts_with(dir_prefix) && dpath != clean_path) {
				is_directory = true;
				break;
			}
		}
	}

	if (is_directory) {
		agentlib::vfs_file_info info;
		info.uri = "system://" + (clean_path.empty() ? "" : (clean_path + "/"));
		info.type = 'D';
		info.size = 0;
		info.size_in_lines = 0;
		std::lock_guard<std::mutex> lock(generators_mutex_);
		auto dit = descriptions_.find(clean_path);
		if (dit == descriptions_.end()) {
			dit = descriptions_.find(clean_path + "/");
		}
		if (dit != descriptions_.end()) {
			info.details = dit->second;
		}
		return info;
	}

	agentlib::vfs_file_info info;
	info.uri = "system://" + clean_path;
	info.type = 'F';
	info.size = 0;
	info.size_in_lines = 0;

	{
		std::lock_guard<std::mutex> lock(generators_mutex_);
		auto git = generators_.find(clean_path);
		if (git != generators_.end() && git->second) {
			std::string gen_content = git->second("");
			info.size = gen_content.size();
			info.size_in_lines = std::count(gen_content.begin(), gen_content.end(), '\n') + 1;
		}
	}

	if (info.size == 0) {
		const auto &docs = get_embedded_system_docs();
		auto it = docs.find(clean_path);
		if (it != docs.end()) {
			info.size = it->second.size();
			info.size_in_lines = std::count(it->second.begin(), it->second.end(), '\n') + 1;
		}
	}

	{
		std::lock_guard<std::mutex> lock(generators_mutex_);
		auto dit = descriptions_.find(clean_path);
		if (dit != descriptions_.end()) {
			info.details = dit->second;
		}
	}

	return info;
}

std::vector<agentlib::vfs_file_info> system_vfs_provider::list_directory(const std::string &prefix) const
{
	std::string norm_prefix = resolve_path(prefix);
	while (norm_prefix.starts_with("/")) {
		norm_prefix = norm_prefix.substr(1);
	}
	if (!norm_prefix.empty() && !norm_prefix.ends_with("/")) {
		norm_prefix += "/";
	}

	if (norm_prefix == "tool-families/") {
		std::vector<agentlib::vfs_file_info> results;
		auto families = agentlib::tool_registry::get_instance().get_all_registered_families();
		std::sort(families.begin(), families.end());
		for (const auto &fam : families) {
			if (fam.starts_with(':')) continue;
			std::string file_uri = "system://tool-families/" + fam + ".md";
			std::string content = generate_tool_family_detail(fam);
			agentlib::vfs_file_info info;
			info.uri = file_uri;
			info.type = 'F';
			info.size = content.size();
			info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
			info.details = get_family_reason_helper(fam);
			results.push_back(info);
		}
		return results;
	}

	if (norm_prefix == "subagents/") {
		std::vector<agentlib::vfs_file_info> results;
		auto active_agents = agentlib::ai_agent::get_all_active_agents();
		for (const auto &ag : active_agents) {
			if (!ag) continue;
			int id = ag->get_id();
			std::string status_str = agentlib::agent_status_to_string(ag->get_status());
			
			agentlib::vfs_file_info dir_info;
			dir_info.uri = "system://subagents/" + std::to_string(id) + "/";
			dir_info.type = 'D';
			dir_info.size = 0;
			dir_info.size_in_lines = 0;
			dir_info.details = "Subagent " + std::to_string(id) + " (" + ag->get_name() + ") - Status: " + status_str;
			results.push_back(dir_info);

			std::string content = generate_subagent_summary(id);
			agentlib::vfs_file_info file_info;
			file_info.uri = "system://subagents/" + std::to_string(id) + ".md";
			file_info.type = 'F';
			file_info.size = content.size();
			file_info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
			file_info.details = "Status dashboard & summary for subagent " + std::to_string(id);
			results.push_back(file_info);
		}
		return results;
	}

	if (norm_prefix.starts_with("subagents/")) {
		std::string sub = norm_prefix.substr(10);
		if (sub.ends_with("/")) sub = sub.substr(0, sub.length() - 1);
		try {
			int id = std::stoi(sub);
			std::vector<agentlib::vfs_file_info> results;
			
			std::string fr_content = generate_subagent_final_result(id);
			agentlib::vfs_file_info fr_info;
			fr_info.uri = "system://subagents/" + std::to_string(id) + "/final_result.md";
			fr_info.type = 'F';
			fr_info.size = fr_content.size();
			fr_info.size_in_lines = std::count(fr_content.begin(), fr_content.end(), '\n') + 1;
			fr_info.details = "Output of report_final_result tool call for subagent " + std::to_string(id);
			results.push_back(fr_info);

			std::string tr_content = generate_subagent_transcript(id, "");
			agentlib::vfs_file_info tr_info;
			tr_info.uri = "system://subagents/" + std::to_string(id) + "/transcript.md";
			tr_info.type = 'F';
			tr_info.size = tr_content.size();
			tr_info.size_in_lines = std::count(tr_content.begin(), tr_content.end(), '\n') + 1;
			tr_info.details = "Step-by-step execution transcript for subagent " + std::to_string(id);
			results.push_back(tr_info);

			return results;
		} catch (...) {}
	}

	std::vector<agentlib::vfs_file_info> results;
	std::map<std::string, bool> seen;

	std::lock_guard<std::mutex> lock(generators_mutex_);

	auto process_path = [&](const std::string &p, size_t size, size_t lines) {
		if (!norm_prefix.empty() && !p.starts_with(norm_prefix)) {
			return;
		}

		std::string_view remainder = p;
		if (!norm_prefix.empty()) {
			remainder = remainder.substr(norm_prefix.length());
		}

		if (remainder.empty()) {
			return;
		}

		size_t slash_pos = remainder.find('/');
		if (slash_pos != std::string_view::npos) {
			std::string dir_name = std::string(remainder.substr(0, slash_pos));
			std::string dir_rel_path = norm_prefix + dir_name;
			std::string dir_uri = "system://" + dir_rel_path + "/";

			if (!seen[dir_uri]) {
				seen[dir_uri] = true;
				agentlib::vfs_file_info info;
				info.uri = dir_uri;
				info.type = 'D';
				info.size = 0;
				info.size_in_lines = 0;

				auto dit = descriptions_.find(dir_rel_path);
				if (dit == descriptions_.end()) {
					dit = descriptions_.find(dir_rel_path + "/");
				}
				if (dit != descriptions_.end()) {
					info.details = dit->second;
				} else if (dir_name == "languages") {
					info.details = "Directory containing language-specific development guidelines and standards.";
				} else if (dir_name == "workflows") {
					info.details = "Directory containing subagent and system workflow guidelines.";
				} else if (dir_name == "tool-families") {
					info.details = "Directory containing tool family activation guides and member tool specifications.";
				} else if (dir_name == "project") {
					info.details = "Directory containing project-level build status, diagnostics, and workspace information.";
				}
				results.push_back(info);
			}
		} else {
			std::string file_uri = "system://" + p;
			if (!seen[file_uri]) {
				seen[file_uri] = true;
				agentlib::vfs_file_info info;
				info.uri = file_uri;
				info.type = 'F';
				info.size = size;
				info.size_in_lines = lines;

				auto dit = descriptions_.find(p);
				if (dit != descriptions_.end()) {
					info.details = dit->second;
				}
				results.push_back(info);
			}
		}
	};

	// 1. Process dynamic generator URIs
	for (const auto &[p, fn] : generators_) {
		size_t size = 0;
		size_t lines = 0;
		if (fn) {
			std::string gen_content = fn("");
			size = gen_content.size();
			lines = std::count(gen_content.begin(), gen_content.end(), '\n') + 1;
		}
		process_path(p, size, lines);
	}

	// 2. Process embedded static docs URIs
	const auto &docs = get_embedded_system_docs();
	for (const auto &[p, content] : docs) {
		size_t lines = std::count(content.begin(), content.end(), '\n') + 1;
		process_path(p, content.size(), lines);
	}

	return results;
}

} // namespace turbostar
