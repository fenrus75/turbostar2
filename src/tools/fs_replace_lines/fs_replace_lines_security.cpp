#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "fs_replace_lines.h"

namespace tools
{

bool fs_replace_lines_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						    std::string &out_error) const
{
	try {
		if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
			out_error = "Missing or invalid 'path' parameter.";
			return false;
		}
		std::string raw_path = raw_json["path"].get<std::string>();

		if (!raw_json.contains("edits") || !raw_json["edits"].is_array() || raw_json["edits"].empty()) {
			out_error = "Missing or empty 'edits' array.";
			return false;
		}

		std::vector<edit_operation> parsed_edits;
		int prev_line = 2000000000;

		for (const auto &edit_json : raw_json["edits"]) {
			if (!edit_json.contains("line_number") || !edit_json["line_number"].is_number_integer()) {
				out_error = "Missing or invalid 'line_number' in edit operation.";
				return false;
			}
			if (!edit_json.contains("type") || !edit_json["type"].is_string()) {
				out_error = "Missing or invalid 'type' in edit operation.";
				return false;
			}

			edit_operation edit;
			edit.line_number = edit_json["line_number"].get<int>();
			edit.type = edit_json["type"].get<std::string>();

			if (edit.line_number < 1) {
				out_error = "line_number must be >= 1.";
				return false;
			}
			if (edit.line_number >= prev_line) {
				out_error = "Edits MUST be sorted in strictly DESCENDING order by line_number to prevent index shifting.";
				return false;
			}
			prev_line = edit.line_number;

			if (edit.type != "add" && edit.type != "remove" && edit.type != "replace") {
				out_error = "Invalid edit type: " + edit.type;
				return false;
			}

			// Strictly enforce original_text presence for remove/replace
			if (edit.type == "remove" || edit.type == "replace") {
				if (!edit_json.contains("original_text") || !edit_json["original_text"].is_string()) {
					out_error = "'original_text' parameter is REQUIRED for '" + edit.type +
						    "' operations. You must provide it.";
					return false;
				}
				edit.original_text = edit_json["original_text"].get<std::string>();
				if (edit.original_text.empty()) {
					out_error = "'original_text' cannot be empty for '" + edit.type + "' operations.";
					return false;
				}
			} else {
				edit.original_text = edit_json.value("original_text", "");
			}

			// Strictly enforce replace_with presence for add/replace
			if (edit.type == "add" || edit.type == "replace") {
				if (!edit_json.contains("replace_with") || !edit_json["replace_with"].is_string()) {
					out_error =
					    "'replace_with' parameter is REQUIRED for '" + edit.type + "' operations. You must provide it.";
					return false;
				}
				edit.replace_with = edit_json["replace_with"].get<std::string>();
			} else {
				edit.replace_with = edit_json.value("replace_with", "");
			}

			parsed_edits.push_back(edit);
		}

		// Perform the file security manager check (access_type::write)
		std::string canonical_path;
		if (!ctx.fs_security.validate_access(raw_path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		args_.path = raw_path;
		args_.safe_path = canonical_path;
		args_.edits = parsed_edits;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> fs_replace_lines_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<fs_replace_lines_tool>(args_);
}

// Register the tool with the global registry
REGISTER_TOOL(fs_replace_lines_validator)

} // namespace tools
