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

bool system_vfs_provider::exists(const std::string &uri) const
{
	std::string path = resolve_path(uri);

	{
		std::lock_guard<std::mutex> lock(generators_mutex_);
		if (generators_.find(path) != generators_.end()) {
			return true;
		}
	}

	const auto &docs = get_embedded_system_docs();
	if (docs.find(path) != docs.end()) {
		return true;
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
	std::string path = resolve_path(uri);

	if (!exists(uri)) {
		return std::nullopt;
	}

	agentlib::vfs_file_info info;
	info.uri = "system://" + path;
	info.type = 'F';
	info.size = 0;
	info.size_in_lines = 0;

	const auto &docs = get_embedded_system_docs();
	auto it = docs.find(path);
	if (it != docs.end()) {
		info.size = it->second.size();
		info.size_in_lines = std::count(it->second.begin(), it->second.end(), '\n') + 1;
	}

	return info;
}

std::vector<agentlib::vfs_file_info> system_vfs_provider::list_directory(const std::string &prefix) const
{
	std::string norm_prefix = resolve_path(prefix);
	if (norm_prefix.length() > 0 && !norm_prefix.ends_with("/")) {
		norm_prefix += "/";
	}

	std::vector<agentlib::vfs_file_info> results;
	std::map<std::string, bool> seen;

	// Add dynamic generator URIs
	{
		std::lock_guard<std::mutex> lock(generators_mutex_);
		for (const auto &[p, fn] : generators_) {
			if (norm_prefix.empty() || p.starts_with(norm_prefix)) {
				agentlib::vfs_file_info info;
				info.uri = "system://" + p;
				info.type = 'F';
				info.size = 0;
				info.size_in_lines = 0;
				results.push_back(info);
				seen[p] = true;
			}
		}
	}

	// Add embedded static docs URIs
	const auto &docs = get_embedded_system_docs();
	for (const auto &[p, content] : docs) {
		if ((norm_prefix.empty() || p.starts_with(norm_prefix)) && !seen[p]) {
			agentlib::vfs_file_info info;
			info.uri = "system://" + p;
			info.type = 'F';
			info.size = content.size();
			info.size_in_lines = std::count(content.begin(), content.end(), '\n') + 1;
			results.push_back(info);
			seen[p] = true;
		}
	}

	return results;
}

} // namespace turbostar
