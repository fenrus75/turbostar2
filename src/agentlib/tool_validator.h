#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include "agent_properties.h"
#include "llm_tool.h"
#include "tool_context.h"

#include "json_utils.h"
#include "utf8.h"

namespace agentlib
{

/*

# subclasses of tool_validator

| subclass                     | filename                                                    |
| ---------------------------- | ----------------------------------------------------------- |
| fs_man_validator             | src/tools/fs_man/fs_man.h                                   |
| fs_replace_content_validator | src/tools/fs_replace_content/fs_replace_content.h           |
| fs_list_dir_validator        | src/tools/fs_list_dir/fs_list_dir.h                          |
| fs_batch_read_validator      | src/tools/fs_read_lines/fs_batch_read_security.cpp          |
| confirm_code_review_item_validator | src/tools/confirm_code_review_item/confirm_code_review_item.h |
| resolve_code_review_item_validator | src/tools/resolve_code_review_item/resolve_code_review_item.h |
| list_code_review_items_validator   | src/tools/list_code_review_items/list_code_review_items.h   |
| get_code_review_item_validator     | src/tools/get_code_review_item/get_code_review_item.h       |
| security_review_with_agent_validator | src/plugins/securityagent/security_review_with_agent/security_review_with_agent.h |
| git_blame_validator          | src/tools/git_blame/git_blame.h                              |
| security_verify_html_validator | src/plugins/securityagent/security_verify_html/security_verify_html.h |
| hexdump_validator | src/plugins/hexedit/hexdump_validator.h |
| hexwrite_validator | src/plugins/hexedit/hexwrite_validator.h |
| apply_text_filter_validator | src/tools/apply_text_filter/apply_text_filter.h |
| image_resize_validator | src/plugins/image_basic/image_resize_validator.h |
| image_crop_validator | src/plugins/image_basic/image_crop_validator.h |
| image_rotate_validator | src/plugins/image_basic/image_rotate_validator.h |
| image_mirror_validator | src/plugins/image_basic/image_mirror_validator.h |
| image_grayscale_validator | src/plugins/image_basic/image_grayscale_validator.h |
| image_threshold_validator | src/plugins/image_basic/image_threshold_validator.h |
| image_import_validator | src/tools/image_import/image_import.h |
| image_export_validator | src/tools/image_export/image_export.h |
| fs_purge_tmp_validator | src/tools/fs_purge_tmp/fs_purge_tmp.h |
| agent_wait_for_app_validator | src/tools/agent_wait_for_app/agent_wait_for_app_security.cpp |
| perform_code_review_validator | src/tools/perform_code_review/perform_code_review.h |
| create_code_review_item_validator | src/tools/create_code_review_item/create_code_review_item.h |
| update_code_review_item_validator | src/tools/update_code_review_item/update_code_review_item.h |
| confirm_code_review_item_validator | src/tools/confirm_code_review_item/confirm_code_review_item.h |
| resolve_code_review_item_validator | src/tools/resolve_code_review_item/resolve_code_review_item.h |
| get_code_review_item_validator | src/tools/get_code_review_item/get_code_review_item.h |
| list_code_review_items_validator | src/tools/list_code_review_items/list_code_review_items.h |
| fs_glob_validator            | src/tools/fs_glob/fs_glob.h                                 |
| fs_find_files_validator      | src/tools/fs_glob/fs_glob.h                                 |
| git_log_validator            | src/tools/git_log/git_log.h                                 |

*/

struct tool_example {
	std::string title;
	nlohmann::json input_args;
	std::string explanation;
};

class tool_validator
{
      public:
	virtual ~tool_validator() = default;

	// Schema definition for the LLM payload
	virtual std::string get_name() const = 0;
	virtual std::string get_description() const = 0;
	virtual nlohmann::json get_parameters_schema() const = 0;
	virtual std::vector<tool_example> get_examples() const
	{
		return {};
	}

	// Indicates if the tool is "pure" (has no persistent side effects on the project codebase).
	// Pure tools (e.g., read, list, status, code review database, scratch VFS) can be safely executed by read-only agents.
	virtual bool is_pure() const
	{
		return false;
	}
	virtual bool is_pure(const nlohmann::json & /*args*/) const
	{
		return is_pure();
	}

	// Returns the tool family name (default is "base")
	virtual std::string get_family() const
	{
		return "base";
	}

	// Indicates if the tool's execution should be hidden from the UI by default.
	virtual bool is_silent_by_default() const
	{
		return is_pure();
	}

	// Indicates if the tool should be exposed over the MCP stdio interface.
	// Defaults to true. Override to return false for TUI/editor-bound tools.
	virtual bool expose_in_mcp() const
	{
		return true;
	}

	// Indicates if the tool can be executed by the given agent properties.
	// By default, summarizers are allowed no tools, while other roles can run any tool.
	virtual bool is_allowed_for_agent(const agent_properties &properties) const;

	// Parameter alias mapping: maps incoming alias parameter names to canonical schema property names.
	// Derived classes may override to declare custom tool-specific aliases.
	virtual std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const
	{
		return {};
	}

	// Non-Virtual Interface (NVI): Enforces state and execution order.
	// Parses and validates args before the tool is allowed to be instantiated.
	bool validate_args(const nlohmann::json &raw_args, const tool_context &ctx, std::string &out_error)
	{
		is_validated_ = false;

		// Centralized Automated Schema Validation
		nlohmann::json schema = get_parameters_schema();
		if (!ctx.mutation_possible) {
			if (schema.contains("properties") && schema["properties"].is_object()) {
				schema["properties"].erase("async");
				schema["properties"].erase("is_async");
			}
			if (schema.contains("required") && schema["required"].is_array()) {
				nlohmann::json new_req = nlohmann::json::array();
				for (const auto &item : schema["required"]) {
					if (item.is_string() &&
					    (item.get<std::string>() == "async" || item.get<std::string>() == "is_async")) {
						continue;
					}
					new_req.push_back(item);
				}
				schema["required"] = new_req;
			}
		}

		// Apply parameter alias rewriting and PascalCase/camelCase -> snake_case normalization
		// to resolve cross-harness variations to canonical parameter names.
		nlohmann::json args = raw_args.is_object() ? raw_args : nlohmann::json::object();
		normalize_object_args(args, schema, get_custom_parameter_aliases());
		validated_args_ = args;

		if (schema.contains("required") && schema["required"].is_array()) {
			for (const auto &req : schema["required"]) {
				if (!req.is_string())
					continue;
				std::string key = req.get<std::string>();
				if (!args.contains(key)) {
					out_error = "Schema Validation Failed: Missing required argument '" + key + "'";
					return false;
				}
			}
		}

		if (schema.contains("properties") && schema["properties"].is_object()) {
			for (auto it = args.begin(); it != args.end(); ++it) {
				if (!schema["properties"].contains(it.key())) {
					out_error = "Schema Validation Failed: Unexpected argument '" + it.key() + "'";
					return false;
				}
				auto prop_schema = schema["properties"][it.key()];
				if (prop_schema.contains("type") && prop_schema["type"].is_string()) {
					std::string expected_type = prop_schema["type"].get<std::string>();
					bool type_ok = false;
					if (expected_type == "string" && it.value().is_string())
						type_ok = true;
					else if (expected_type == "integer") {
						if (it.value().is_number_integer()) {
							type_ok = true;
						} else if (it.value().is_string()) {
							std::string dummy_err;
							int64_t dummy_val = 0;
							if (json_utils::get_number(args, it.key(), dummy_val, int64_t(0), dummy_err)) {
								type_ok = true;
							}
						}
					} else if (expected_type == "boolean" && it.value().is_boolean())
						type_ok = true;
					else if (expected_type == "array") {
						if (it.value().is_array()) {
							type_ok = true;
						} else if (it.value().is_string()) {
							it.value() = nlohmann::json::array({it.value().get<std::string>()});
							type_ok = true;
						} else if (it.value().is_object()) {
							it.value() = nlohmann::json::array({it.value()});
							type_ok = true;
						}
					} else if (expected_type == "object" && it.value().is_object())
						type_ok = true;
					else if (expected_type == "number") {
						if (it.value().is_number()) {
							type_ok = true;
						} else if (it.value().is_string()) {
							std::string dummy_err;
							double dummy_val = 0.0;
							if (json_utils::get_number(args, it.key(), dummy_val, 0.0, dummy_err)) {
								type_ok = true;
							}
						}
					}

					if (!type_ok) {
						out_error = "Schema Validation Failed: Type mismatch for argument '" + it.key() +
							    "'. Expected " + expected_type;
						return false;
					}
				}
			}
		}

		validated_args_ = args;

		// Delegate to tool-specific validation
		if (validate_args_impl(args, ctx, out_error)) {
			is_validated_ = true;
			return true;
		}
		return false;
	}

	// Instantiates the actual tool. STRICTLY FAILS if validate_args was not successful.
	std::unique_ptr<llm_tool> create_tool(const nlohmann::json &args) const
	{
		if (!is_validated_) {
			return nullptr;
		}
		// If args is empty or matches original, prioritize the alias-rewritten validated_args_
		if (!validated_args_.is_null()) {
			return create_tool_impl(validated_args_);
		}
		return create_tool_impl(args);
	}

	const nlohmann::json &get_validated_args() const
	{
		return validated_args_;
	}

	// Normalizes incoming JSON argument keys for a schema object by applying:
	// 1. Standard and tool-specific vocabulary aliases (e.g. target_file/absolute_path -> path, command_line -> command).
	// 2. Automatic PascalCase/camelCase -> snake_case conversion (e.g. StartLine/startLine -> start_line).
	// 3. Recursive normalization for nested objects and array-of-object items (e.g. chunks, edits, items).
	static void normalize_object_args(nlohmann::json &args, const nlohmann::json &schema,
					  const std::unordered_map<std::string, std::string> &custom_aliases = {})
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
		register_aliases("query", {"pattern", "search_query", "sql", "question"});
		register_aliases("command", {"command_line", "cmd", "shell_command", "script"});
		register_aliases("content", {"code_content", "text", "file_content", "data", "body"});
		register_aliases("force_overwrite", {"overwrite"});
		register_aliases("target_content",
				 {"old_string", "old_str", "old_text", "original_text", "target", "search"});
		register_aliases("replacement_content",
				 {"new_string", "new_str", "new_text", "replace_with", "replacement", "replace"});
		register_aliases("chunks", {"replacement_chunks", "edits", "replacements"});
		register_aliases("start_line", {"start", "from_line", "begin_line", "offset_line"});
		register_aliases("end_line", {"end", "to_line", "stop_line"});
		register_aliases("start", {"start_line", "from_line", "begin_line", "offset_line"});
		register_aliases("end", {"end_line", "to_line", "stop_line"});
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

      protected:
	// Derived classes MUST implement these protected methods instead of the public ones.
	virtual bool validate_args_impl(const nlohmann::json &args, const tool_context &ctx, std::string &out_error) const = 0;
	virtual std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &args) const = 0;

      private:
	bool is_validated_ = false;
	nlohmann::json validated_args_;
};

} // namespace agentlib
