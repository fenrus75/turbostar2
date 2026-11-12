#include "tool_validator.h"
#include "../codereview_manager.h"
#include "../project_manager.h"
#include "utf8.h"
#include <algorithm>

namespace agentlib {

void tool_validator::normalize_object_args(nlohmann::json &args, const nlohmann::json &schema,
					  const std::unordered_map<std::string, std::string> &custom_aliases)
{
	if (!schema.contains("properties") || !schema["properties"].is_object() || !args.is_object()) {
		return;
	}

	const auto &props = schema["properties"];
	std::unordered_map<std::string, std::string> alias_map;

	auto register_aliases = [&](const std::string &canonical, std::initializer_list<const char *> aliases) {
		if (!props.contains(canonical)) {
			return;
		}
		for (const char *alias : aliases) {
			if (!props.contains(alias)) {
				alias_map[alias] = canonical;
			}
		}
	};

	// 1. Standard global parameter aliases according to docs/tools.md and external harness conventions:
	register_aliases("path", {"file_path", "filepath", "filename", "file", "target_file", "absolute_path",
				  "target_path", "source_path", "input_path", "root_dir", "dir", "directory",
				  "dir_path", "directory_path", "search_directory"});
	register_aliases("search_path", {"path", "file_path", "filepath", "filename", "file", "target_file",
					 "absolute_path", "root_dir", "dir", "directory", "dir_path",
					 "directory_path", "search_directory", "search_dir", "folder"});
	register_aliases("pattern", {"query", "search_query", "regex", "regexp", "file_pattern", "glob",
				     "include_pattern", "text"});
	if ((args.contains("regex") || args.contains("regexp")) && props.contains("is_regex") && !args.contains("is_regex")) {
		args["is_regex"] = true;
	}
	register_aliases("query", {"pattern", "search_query", "sql", "question"});
	register_aliases("command", {"command_line", "cmd", "shell_command", "script"});
	register_aliases("content", {"code_content", "text", "file_content", "data", "body"});
	register_aliases("force_overwrite", {"overwrite"});
	register_aliases("target_content",
			 {"old_string", "old_str", "old_text", "original_text", "target", "search"});
	register_aliases("replacement_content",
			 {"new_string", "new_str", "new_text", "replace_with", "replacement", "replace"});
	register_aliases("original_text",
			 {"old_content", "target_content", "old_string", "old_str", "old_text", "target"});
	register_aliases("replace_with",
			 {"new_content", "content", "replacement", "replacement_content", "new_string", "new_str", "new_text"});
	register_aliases("new_content",
			 {"replace_with", "content", "replacement", "replacement_content", "new_string", "new_str", "new_text"});
	register_aliases("chunks", {"replacement_chunks", "edits", "replacements"});
	register_aliases("start_line", {"start", "from_line", "begin_line", "offset_line", "line_start", "first_line", "line_number", "line"});
	register_aliases("end_line", {"end", "to_line", "stop_line", "line_end", "last_line"});
	register_aliases("line_number", {"start_line", "line_start", "line", "start", "from_line", "begin_line", "first_line"});
	register_aliases("start", {"start_line", "from_line", "begin_line", "offset_line", "line_start", "first_line"});
	register_aliases("end", {"end_line", "to_line", "stop_line", "line_end", "last_line"});
	register_aliases("url", {"uri", "link", "web_url"});
	register_aliases("async", {"is_async", "run_in_background", "is_background", "background"});
	register_aliases("case_insensitive", {"ignore_case"});
	register_aliases("size", {"length", "bytes", "num_bytes", "byte_count"});
	register_aliases("length",
			 {"lines", "line_count", "num_lines", "lines_count", "num_items", "max_lines", "count"});
	register_aliases("count", {"lines", "line_count", "num_lines", "lines_count", "num_items", "max_lines",
				   "length", "limit"});
	register_aliases("offset", {"start_offset", "start_byte", "byte_offset", "content_offset"});
	register_aliases("limit", {"count", "max_results", "max_count", "n", "max_commits", "max_items"});
	register_aliases("test_names", {"test_name", "test", "tests"});
	if (props.contains("test_name") && !props.contains("test_names")) {
		alias_map["test_names"] = "test_name";
	}
	register_aliases("paths", {"files", "file_paths", "filepaths", "file", "path", "target_files"});
	register_aliases("files", {"paths", "file_paths", "filepaths", "file", "path", "target_files"});
	if (props.contains("args") && !props.contains("arguments")) {
		alias_map["arguments"] = "args";
	}
	if (props.contains("arguments") && !props.contains("args")) {
		alias_map["args"] = "arguments";
	}

	// Map snake_case equivalents of any non-snake_case schema properties (e.g. "newX" <- "new_x", "URL" <- "url")
	for (auto it = props.begin(); it != props.end(); ++it) {
		std::string snake_prop = utf8::to_snake_case(it.key());
		if (snake_prop != it.key() && !props.contains(snake_prop)) {
			alias_map[snake_prop] = it.key();
		}
	}

	// 2. Merge tool-specific custom aliases (only when target canonical key exists in this schema level):
	for (const auto &[alias, canonical] : custom_aliases) {
		if (!props.contains(canonical)) {
			continue;
		}
		alias_map[alias] = canonical;
		std::string snake_alias = utf8::to_snake_case(alias);
		if (!props.contains(snake_alias)) {
			alias_map[snake_alias] = canonical;
		}
	}

	// 3. Rewrite arguments: resolve direct aliases as well as PascalCase/camelCase variations.
	std::vector<std::string> non_canonical_keys;
	for (auto it = args.begin(); it != args.end(); ++it) {
		if (!props.contains(it.key())) {
			non_canonical_keys.push_back(it.key());
		}
	}

	for (const auto &key : non_canonical_keys) {
		std::string canonical;
		if (auto it = alias_map.find(key); it != alias_map.end()) {
			canonical = it->second;
		} else {
			std::string snake_key = utf8::to_snake_case(key);
			if (props.contains(snake_key)) {
				canonical = snake_key;
			} else if (auto sit = alias_map.find(snake_key); sit != alias_map.end()) {
				canonical = sit->second;
			}
		}

		if (!canonical.empty()) {
			if (!args.contains(canonical)) {
				args[canonical] = args[key];
			}
			args.erase(key);
		}
	}

	// 4. Recursively normalize nested objects and arrays of objects (e.g. chunks, edits, items)
	for (auto it = props.begin(); it != props.end(); ++it) {
		const std::string &prop_key = it.key();
		if (!args.contains(prop_key) || !it.value().is_object()) {
			continue;
		}
		const auto &prop_schema = it.value();
		if (args[prop_key].is_object() && prop_schema.contains("properties")) {
			normalize_object_args(args[prop_key], prop_schema, custom_aliases);
		} else if (args[prop_key].is_object() && prop_schema.contains("items") &&
			   prop_schema["items"].is_object() && prop_schema["items"].contains("properties")) {
			normalize_object_args(args[prop_key], prop_schema["items"], custom_aliases);
		} else if (args[prop_key].is_array() && prop_schema.contains("items") &&
			   prop_schema["items"].is_object() && prop_schema["items"].contains("properties")) {
			for (auto &elem : args[prop_key]) {
				if (elem.is_object()) {
					normalize_object_args(elem, prop_schema["items"], custom_aliases);
				}
			}
		}
	}
}

static std::vector<std::string> split_families(const std::string &s) {
	std::vector<std::string> res;
	size_t start = 0, end;
	while ((end = s.find('|', start)) != std::string::npos) {
		res.push_back(s.substr(start, end - start));
		start = end + 1;
	}
	res.push_back(s.substr(start));
	return res;
}

bool tool_validator::is_allowed_for_agent(const agent_properties &properties) const {
	std::string family_str = get_family();
	for (const auto &fam : split_families(family_str)) {
		if (fam == "base") {
			return true;
		}
		if (std::find(properties.active_families.begin(), properties.active_families.end(), fam) != properties.active_families.end()) {
			return true;
		}
		if (fam == "code_review" && codereview_manager::get_instance().has_active_items()) {
			return true;
		}
		if (fam == "editor" && project_manager::get_instance().is_editor_mode()) {
			return true;
		}
	}
	return false;
}

} // namespace agentlib
