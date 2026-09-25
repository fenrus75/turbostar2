#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "fs_replace_lines.h"

#include "../../agentlib/ai_agent.h"

#include "../../agentlib/json_utils.h"

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

		nlohmann::json edits_array;
		if (raw_json.contains("edits") && raw_json["edits"].is_array() && !raw_json["edits"].empty()) {
			edits_array = raw_json["edits"];
		} else {
			// Single-edit shortcut: construct edits array from top-level arguments
			int start_line = 0;
			if (raw_json.contains("start_line") && raw_json["start_line"].is_number()) {
				start_line = raw_json["start_line"].get<int>();
			} else if (raw_json.contains("line_number") && raw_json["line_number"].is_number()) {
				start_line = raw_json["line_number"].get<int>();
			} else if (raw_json.contains("line") && raw_json["line"].is_number()) {
				start_line = raw_json["line"].get<int>();
			}

			if (start_line >= 1) {
				nlohmann::json single_edit = {{"line_number", start_line}};
				if (raw_json.contains("end_line") && raw_json["end_line"].is_number()) {
					single_edit["end_line"] = raw_json["end_line"].get<int>();
				}
				if (raw_json.contains("type") && raw_json["type"].is_string()) {
					single_edit["type"] = raw_json["type"].get<std::string>();
				}
				if (raw_json.contains("original_text") && raw_json["original_text"].is_string()) {
					single_edit["original_text"] = raw_json["original_text"].get<std::string>();
				} else if (raw_json.contains("old_content") && raw_json["old_content"].is_string()) {
					single_edit["original_text"] = raw_json["old_content"].get<std::string>();
				} else if (raw_json.contains("target_content") && raw_json["target_content"].is_string()) {
					single_edit["original_text"] = raw_json["target_content"].get<std::string>();
				}

				if (raw_json.contains("replace_with") && raw_json["replace_with"].is_string()) {
					single_edit["replace_with"] = raw_json["replace_with"].get<std::string>();
				} else if (raw_json.contains("new_content") && raw_json["new_content"].is_string()) {
					single_edit["replace_with"] = raw_json["new_content"].get<std::string>();
				} else if (raw_json.contains("content") && raw_json["content"].is_string()) {
					single_edit["replace_with"] = raw_json["content"].get<std::string>();
				} else if (raw_json.contains("replacement") && raw_json["replacement"].is_string()) {
					single_edit["replace_with"] = raw_json["replacement"].get<std::string>();
				}

				edits_array = nlohmann::json::array({single_edit});
			} else {
				out_error = "Missing required 'edits' array or single-edit line number ('start_line').";
				return false;
			}
		}

		// Perform the file security manager check (access_type::write)
		std::string real_check_path = raw_path;
		if (real_check_path.find("file://") == 0) {
			real_check_path = real_check_path.substr(7);
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(real_check_path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		// Read file lines lazily if needed to auto-populate original_text from line numbers
		std::vector<std::string> file_lines;
		auto load_file_lines = [&]() {
			if (!file_lines.empty())
				return;
			std::string path_to_read = canonical_path;
			auto *vfs = ctx.fs_security.get_vfs();
			if (vfs && vfs->is_local_path_available(canonical_path)) {
				path_to_read = vfs->get_local_path(canonical_path);
			}
			std::ifstream in(path_to_read);
			if (in.is_open()) {
				std::string line;
				while (std::getline(in, line)) {
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					file_lines.push_back(line);
				}
			}
		};

		std::vector<edit_operation> parsed_edits;

		for (const auto &edit_json : edits_array) {
			int line_number = 0;
			if (edit_json.contains("line_number") && edit_json["line_number"].is_number()) {
				line_number = edit_json["line_number"].get<int>();
			} else if (edit_json.contains("start_line") && edit_json["start_line"].is_number()) {
				line_number = edit_json["start_line"].get<int>();
			} else if (edit_json.contains("line") && edit_json["line"].is_number()) {
				line_number = edit_json["line"].get<int>();
			} else {
				out_error = "Missing or invalid 'line_number' in edit operation (must be >= 1).";
				return false;
			}

			if (line_number < 1) {
				out_error = "line_number must be >= 1.";
				return false;
			}

			edit_operation edit;
			edit.line_number = line_number;

			if (edit_json.contains("type") && edit_json["type"].is_string()) {
				edit.type = edit_json["type"].get<std::string>();
			} else {
				edit.type = "replace";
			}

			if (edit.type != "add" && edit.type != "remove" && edit.type != "replace") {
				out_error = "Invalid edit type: " + edit.type;
				return false;
			}

			// original_text extraction with alias support
			std::string orig_text;
			if (edit_json.contains("original_text") && edit_json["original_text"].is_string()) {
				orig_text = edit_json["original_text"].get<std::string>();
			} else if (edit_json.contains("old_content") && edit_json["old_content"].is_string()) {
				orig_text = edit_json["old_content"].get<std::string>();
			} else if (edit_json.contains("target_content") && edit_json["target_content"].is_string()) {
				orig_text = edit_json["target_content"].get<std::string>();
			}

			// If original_text is omitted but end_line is specified, auto-populate from file
			int end_line = line_number;
			if (edit_json.contains("end_line") && edit_json["end_line"].is_number()) {
				end_line = edit_json["end_line"].get<int>();
			}

			if (orig_text.empty() && (edit.type == "remove" || edit.type == "replace")) {
				load_file_lines();
				if (!file_lines.empty() && line_number >= 1 && line_number <= static_cast<int>(file_lines.size())) {
					int actual_end = std::min(end_line, static_cast<int>(file_lines.size()));
					actual_end = std::max(line_number, actual_end);
					for (int l = line_number; l <= actual_end; ++l) {
						if (!orig_text.empty())
							orig_text += "\n";
						orig_text += file_lines[l - 1];
					}
				}
			}

			// Strictly enforce original_text presence for remove/replace
			if (edit.type == "remove" || edit.type == "replace") {
				if (orig_text.empty()) {
					out_error = "'original_text' parameter is REQUIRED for '" + edit.type +
						    "' operations. You must provide it or specify a valid line range.";
					return false;
				}
				edit.original_text = orig_text;
			} else {
				edit.original_text = orig_text;
			}

			// replace_with extraction with alias support
			std::string repl_with;
			if (edit_json.contains("replace_with") && edit_json["replace_with"].is_string()) {
				repl_with = edit_json["replace_with"].get<std::string>();
			} else if (edit_json.contains("new_content") && edit_json["new_content"].is_string()) {
				repl_with = edit_json["new_content"].get<std::string>();
			} else if (edit_json.contains("content") && edit_json["content"].is_string()) {
				repl_with = edit_json["content"].get<std::string>();
			} else if (edit_json.contains("replacement") && edit_json["replacement"].is_string()) {
				repl_with = edit_json["replacement"].get<std::string>();
			}

			if (edit.type == "add" || edit.type == "replace") {
				edit.replace_with = repl_with;
			} else {
				edit.replace_with = repl_with;
			}

			if (!edit.original_text.empty()) {
				int newlines = std::count(edit.original_text.begin(), edit.original_text.end(), '\n');
				edit.lines_to_remove = newlines + 1;
			} else {
				edit.lines_to_remove = 1;
			}

			parsed_edits.push_back(edit);
		}

		args_.path = raw_path;
		args_.safe_path = canonical_path;
		args_.edits = parsed_edits;

		if (raw_json.contains("strict")) {
			if (!raw_json["strict"].is_boolean()) {
				out_error = "Invalid 'strict' parameter (must be a boolean).";
				return false;
			}
			args_.strict = raw_json["strict"].get<bool>();
		}

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

std::vector<agentlib::tool_example> fs_replace_lines_validator::get_examples() const
{
	return {{"Descending Line Number Mixed Operations (Add, Replace, Remove)",
		 nlohmann::json{
		     {"path", "src/config.cpp"},
		     {"edits",
		      nlohmann::json::array(
			  {nlohmann::json{{"line_number", 80}, {"type", "add"}, {"replace_with", "    bool is_debug = true;\n"}},
			   nlohmann::json{{"line_number", 45},
					  {"type", "replace"},
					  {"original_text", "int timeout = 5;"},
					  {"replace_with", "int timeout = 10;"}},
			   nlohmann::json{{"line_number", 12}, {"type", "remove"}, {"original_text", "#include <legacy_header.h>\n"}}})}},
		 "Applies line edits in strictly DESCENDING line_number order (80 -> 45 -> 12) so bottom insertions do not shift top line "
		 "indexes."}};
}

// Register the tool with the global registry
REGISTER_TOOL(fs_replace_lines_validator)

} // namespace tools
