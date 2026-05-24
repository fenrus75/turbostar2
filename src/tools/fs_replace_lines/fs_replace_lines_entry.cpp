#include <dtl/dtl.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../agentlib/interactions/base.h"
#include "fs_replace_lines.h"

namespace tools
{

class interaction_fs_replace_lines : public agentlib::agent_interaction
{
      public:
	interaction_fs_replace_lines(const std::string &path, size_t num_edits)
	{
		set_boxed(true, 5, path); // 5 is Window Border color pair
		call_text_ = "Applying " + std::to_string(num_edits) + " operation" + (num_edits == 1 ? "" : "s");
	}

	void set_result(const std::string &res)
	{
		result_text_ = res;
		invalidate_cache();
	}

	void set_target_type(const std::string &path, bool is_buffer)
	{
		std::string title = path;
		if (is_buffer)
			title += " (edit buffer)";
		set_boxed(true, 5, title);
	}

	void set_diff(const std::vector<std::string> &before, const std::vector<std::string> &after)
	{
		dtl::Diff<std::string, std::vector<std::string>> d(before, after);
		d.compose();
		d.composeUnifiedHunks();

		std::stringstream ss;
		d.printUnifiedFormat(ss);

		std::string line;
		diff_lines_.clear();
		while (std::getline(ss, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			diff_lines_.push_back(line);
		}
		invalidate_cache();
	}

	std::string get_raw_text() const override
	{
		std::string raw = call_text_;
		if (!result_text_.empty()) {
			raw += "\nResult: " + result_text_;
		}
		for (const auto &dl : diff_lines_) {
			raw += "\n" + dl;
		}
		return raw;
	}

      protected:
	std::vector<agentlib::interaction_line> format_lines(int width) const override
	{
		auto lines = wrap_text("", call_text_, width, 3); // 3 is Yellow on Dark Blue

		if (!diff_lines_.empty()) {
			// Draw a subtle separator
			lines.push_back({std::string(std::min(width, 20), '-'), 8}); // 8 is Selection Cyan

			for (const auto &dl : diff_lines_) {
				int color = 3; // Default Yellow on Dark Blue
				if (dl.empty()) {
					lines.push_back({"", color});
					continue;
				}

				if (dl[0] == '-')
					color = 31; // Bright Red on Dark Blue
				else if (dl[0] == '+')
					color = 30; // Bright Green on Dark Blue
				else if (dl.length() > 2 && dl[0] == '@' && dl[1] == '@')
					color = 32; // Bright Cyan on Dark Blue

				auto dl_wrapped = wrap_text("", dl, width, color);
				lines.insert(lines.end(), dl_wrapped.begin(), dl_wrapped.end());
			}
		}

		if (!result_text_.empty() && result_text_.find("Successfully") != 0) {
			lines.push_back({"", 3});
			auto res_lines = wrap_text("", "-> " + result_text_, width, 10);
			lines.insert(lines.end(), res_lines.begin(), res_lines.end());
		}
		return lines;
	}

      private:
	std::string call_text_;
	std::string result_text_;
	std::vector<std::string> diff_lines_;
};

fs_replace_lines_tool::fs_replace_lines_tool(fs_replace_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<interaction_fs_replace_lines>(args_.path, args_.edits.size());
}

std::shared_ptr<agentlib::agent_interaction> fs_replace_lines_tool::get_interaction() const
{
	return interaction_;
}

bool fs_replace_lines_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	// 1. Existence Check
	if (!std::filesystem::exists(args_.safe_path)) {
		out_error = "Error: File does not exist. fs_replace_lines can only edit existing files.";
		return false;
	}

	// 3. ATOMIC VERIFICATION: Verify ALL orgstrings before making any edits
	std::vector<std::string> lines;
	std::ifstream in(args_.safe_path);
	if (!in.is_open()) {
		out_error = "Error: Could not open file for reading during verification.";
		return false;
	}

	std::string line_content;
	while (std::getline(in, line_content)) {
		if (!line_content.empty() && line_content.back() == '\r') {
			line_content.pop_back();
		}
		lines.push_back(line_content);
	}
	in.close();

	for (const auto &edit : args_.edits) {
		if (edit.type == "add")
			continue;

		int idx = edit.line_number - 1;
		if (idx < 0 || idx >= static_cast<int>(lines.size())) {
			out_error = "Verification Error: line_number " + std::to_string(edit.line_number) + " is out of bounds.";
			return false;
		}

		std::string actual_content = lines[idx];
		std::string expected_prefix = edit.original_text;

		while (!expected_prefix.empty() && std::isspace(expected_prefix.back())) {
			expected_prefix.pop_back();
		}

		if (actual_content.find(expected_prefix) != 0) {
			out_error = "Verification Error at line " + std::to_string(edit.line_number) + ". \nExpected starting with: '" +
				    expected_prefix + "'\nActual content: '" + actual_content + "'";
			return false;
		}
	}

	return true;
}

std::string fs_replace_lines_tool::execute(agentlib::tool_context &ctx)
{
	std::string result_msg;
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
		// Create a JSON payload of the edits to send to the UI thread
		nlohmann::json edits_json = nlohmann::json::array();
		for (const auto &edit : args_.edits) {
			nlohmann::json edit_json;
			edit_json["line_number"] = edit.line_number;
			edit_json["type"] = edit.type;
			edit_json["original_text"] = edit.original_text;
			edit_json["replace_with"] = edit.replace_with;
			edits_json.push_back(edit_json);
		}

		// Note: For live edits in the UI, diffing is harder to synchronously capture
		// since the doc_provider applies it asynchronously. For now, we only
		// compute the rich diff in the disk fallback mode, or we can fetch the file contents here if needed.
		// Let's fallback to disk if we want the diff. Actually, doc_provider->apply_live_edits could be synchronous, but reading
		// before/after is safer via disk fallback for the agent's view. For the sake of this feature, we will execute the disk
		// fallback first to compute the diff and then still apply the live edit if open.
	}

	// We always compute the diff via the disk logic so the agent UI sees what happened
	result_msg = execute_disk_fallback(ctx);

	bool is_buffer = false;
	if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
		is_buffer = true;
		// The file is already modified on disk by the fallback, but the editor buffer needs to know.
		// A better architecture would have the doc_provider return the diff, but for now we just
		// reload the file from disk if it was modified, or let the live_edits handle it.
		// Actually, applying edits_json is the correct way for the editor.
		nlohmann::json edits_json = nlohmann::json::array();
		for (const auto &edit : args_.edits) {
			nlohmann::json edit_json;
			edit_json["line_number"] = edit.line_number;
			edit_json["type"] = edit.type;
			edit_json["original_text"] = edit.original_text;
			edit_json["replace_with"] = edit.replace_with;
			edits_json.push_back(edit_json);
		}
		ctx.doc_provider->apply_live_edits(args_.safe_path, edits_json.dump());
	}

	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_replace_lines>(interaction_)) {
		custom_interaction->set_target_type(args_.path, is_buffer);
		custom_interaction->set_result(result_msg);
		if (ctx.trigger_ui_update) {
			ctx.trigger_ui_update();
		}
	}

	return result_msg;
}

std::string fs_replace_lines_tool::execute_disk_fallback(agentlib::tool_context &ctx)
{
	std::vector<std::string> lines;

	// 1. Read file into memory (we know it's valid from validate_runtime)
	std::ifstream in(args_.safe_path);
	if (!in.is_open()) {
		return "Error: Could not open file for reading during execution.";
	}

	std::string line_content;
	while (std::getline(in, line_content)) {
		if (!line_content.empty() && line_content.back() == '\r') {
			line_content.pop_back();
		}
		lines.push_back(line_content);
	}
	in.close();

	std::vector<std::string> before_lines = lines;

	// 2. Apply edits (Guaranteed descending order and verified by validator)
	for (const auto &edit : args_.edits) {
		int idx = edit.line_number - 1;

		if (edit.type == "remove") {
			lines.erase(lines.begin() + idx);
		} else if (edit.type == "add") {
			// Split newstring by newlines to support multiline insertions
			std::vector<std::string> new_parts;
			if (edit.replace_with.empty()) {
				new_parts.push_back("");
			} else {
				std::stringstream ss(edit.replace_with);
				std::string part;
				while (std::getline(ss, part)) {
					if (!part.empty() && part.back() == '\r')
						part.pop_back();
					new_parts.push_back(part);
				}
			}
			lines.insert(lines.begin() + idx, new_parts.begin(), new_parts.end());

		} else if (edit.type == "replace") {
			// Replace the current line with the new lines
			lines.erase(lines.begin() + idx);

			std::vector<std::string> new_parts;
			if (edit.replace_with.empty()) {
				new_parts.push_back("");
			} else {
				std::stringstream ss(edit.replace_with);
				std::string part;
				while (std::getline(ss, part)) {
					if (!part.empty() && part.back() == '\r')
						part.pop_back();
					new_parts.push_back(part);
				}
			}

			lines.insert(lines.begin() + idx, new_parts.begin(), new_parts.end());
		}
	}

	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_replace_lines>(interaction_)) {
		custom_interaction->set_diff(before_lines, lines);
		if (ctx.trigger_ui_update) {
			ctx.trigger_ui_update();
		}
	}

	// 4. Write back to disk
	std::ofstream out(args_.safe_path, std::ios::binary);
	if (!out.is_open()) {
		return "Error: Could not open file for writing.";
	}

	for (size_t i = 0; i < lines.size(); ++i) {
		out << lines[i];
		if (i < lines.size() - 1 || true) { // Always end with newline
			out << "\n";
		}
	}
	out.close();

	return "Successfully applied " + std::to_string(args_.edits.size()) + " edits to " + args_.path;
}

} // namespace tools
