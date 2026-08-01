#include "vfs/system_vfs_provider.h"
#include "agentlib/subagent_manager.h"
#include "agentlib/tool_registry.h"
#include "mcp/mcp_manager.h"
#include "system_docs_embedded.h"
#include "fs_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;

namespace turbostar {

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
	register_description("mcp.md", "Read to check active Model Context Protocol (MCP) server connections, transport types, and status.");
	// Register directory purpose descriptions
	register_description("languages", "Directory containing language-specific development guidelines and standards.");
	register_description("workflows", "Directory containing subagent and system workflow guidelines.");
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

	if (path.empty()) {
		return true;
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
