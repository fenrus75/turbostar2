#include <algorithm>
#include <format>
#include <dtl/dtl.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../agentlib/interactions/base.h"
#include "../../markdown_utils.h"
#include "fs_replace_lines.h"

namespace tools
{

class interaction_fs_replace_lines : public agentlib::agent_interaction
{
      public:
	interaction_fs_replace_lines(const std::string &path, size_t num_edits)
	{
		call_text_ = "Applying " + std::to_string(num_edits) + " operation" + (num_edits == 1 ? "" : "s") + " to " + path;
	}

	agentlib::interaction_type get_type() const override { return agentlib::interaction_type::action; }
	agentlib::interaction_role get_role() const override { return agentlib::interaction_role::agent; }

	bool needs_subpanel_header() const override { return true; }
	std::string get_subpanel_label() const override { return "Applying edits"; }

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
	std::vector<agentlib::interaction_line> format_lines(int width, agentlib::background_mode bg) const override
	{
		int label_color = get_color_pair(agentlib::interaction_role::thinking, bg);
		auto lines = wrap_text("", call_text_, width, label_color);

		if (!diff_lines_.empty()) {
			// Draw a subtle separator
			lines.push_back({std::string(std::min(width, 20), '-'), label_color});

			for (const auto &dl : diff_lines_) {
				int color = 3; // Default Yellow on Dark Blue
				if (dl.empty()) {
					lines.push_back({std::string(width, ' '), color});
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

		if (!result_text_.empty()) {
			int res_color = get_color_pair(agentlib::interaction_role::agent, bg);
			if (result_text_.find("Successfully") != 0) {
				res_color = get_color_pair(agentlib::interaction_role::error, bg);
			}
			lines.push_back({"", res_color});
			auto res_lines = wrap_text("", "-> " + result_text_, width, res_color);
			lines.insert(lines.end(), res_lines.begin(), res_lines.end());
		}

		for (auto &line : lines) {
			int len = markdown_utils::utf8_length(line.text);
			if (len < width) {
				line.text += std::string(width - len, ' ');
			}
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

	std::vector<std::string> mismatch_errors;

	for (const auto &edit : args_.edits) {
		int idx = edit.line_number - 1;
		int max_idx = (edit.type == "add") ? static_cast<int>(lines.size()) : static_cast<int>(lines.size()) - 1;
		if (idx < 0 || idx > max_idx) {
			mismatch_errors.push_back(std::format("Verification Error: line_number {} is out of bounds.", edit.line_number));
			continue;
		}

		if (edit.type == "add")
			continue;

		std::vector<std::string> expected_lines;
		std::stringstream ss(edit.original_text);
		std::string part;
		while (std::getline(ss, part)) {
			if (!part.empty() && part.back() == '\r') part.pop_back();
			expected_lines.push_back(part);
		}
		if (expected_lines.empty()) expected_lines.push_back("");

		if (idx + static_cast<int>(expected_lines.size()) > static_cast<int>(lines.size())) {
			mismatch_errors.push_back(std::format("Verification Error: Multi-line original_text starting at line_number {} extends beyond the end of the file.", edit.line_number));
			continue;
		}

		bool block_matches = true;
		for (size_t i = 0; i < expected_lines.size(); ++i) {
			std::string actual = lines[idx + i];
			std::string expected = expected_lines[i];
			while (!expected.empty() && std::isspace(expected.back())) expected.pop_back();
			if (actual.find(expected) != 0) {
				block_matches = false;
				break;
			}
		}

		if (!block_matches) {
			// Offset hint logic: check +/- 25 lines
			int found_line = -1;
			int check_radius = 25;
			
			// Start checking nearest lines first
			for (int offset = 1; offset <= check_radius; ++offset) {
				// Check down
				int check_idx_down = idx + offset;
				if (check_idx_down + static_cast<int>(expected_lines.size()) <= static_cast<int>(lines.size())) {
					bool match = true;
					for (size_t i = 0; i < expected_lines.size(); ++i) {
						std::string actual = lines[check_idx_down + i];
						std::string expected = expected_lines[i];
						while (!expected.empty() && std::isspace(expected.back())) expected.pop_back();
						if (actual.find(expected) != 0) {
							match = false;
							break;
						}
					}
					if (match) {
						found_line = check_idx_down + 1;
						break;
					}
				}
				// Check up
				int check_idx_up = idx - offset;
				if (check_idx_up >= 0) {
					bool match = true;
					for (size_t i = 0; i < expected_lines.size(); ++i) {
						std::string actual = lines[check_idx_up + i];
						std::string expected = expected_lines[i];
						while (!expected.empty() && std::isspace(expected.back())) expected.pop_back();
						if (actual.find(expected) != 0) {
							match = false;
							break;
						}
					}
					if (match) {
						found_line = check_idx_up + 1;
						break;
					}
				}
			}

			if (found_line != -1) {
				mismatch_errors.push_back(std::format("Verification Error: The block you provided is not at line {}, but it matches starting at line {}. Please update your line_number.", edit.line_number, found_line));
			} else {
				mismatch_errors.push_back(std::format("Verification Error at line {}. \nExpected starting with: '{}'\nActual content: '{}'", edit.line_number, expected_lines[0], lines[idx]));
			}
		}
	}

	if (!mismatch_errors.empty()) {
		out_error = "";
		for (size_t i = 0; i < mismatch_errors.size(); ++i) {
			out_error += (i > 0 ? "\n\n" : "") + mismatch_errors[i];
		}
		return false;
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
	                lines.erase(lines.begin() + idx, lines.begin() + idx + edit.lines_to_remove);
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
	                lines.erase(lines.begin() + idx, lines.begin() + idx + edit.lines_to_remove);

	                std::vector<std::string> new_parts;			if (edit.replace_with.empty()) {
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

	dtl::Diff<std::string, std::vector<std::string>> d(before_lines, lines);
	d.compose();
	d.composeUnifiedHunks();
	auto hunks = d.getUniHunks();

	std::vector<std::pair<int, int>> ranges;
	for (const auto &hunk : hunks) {
		int start = static_cast<int>(hunk.c);
		int end = static_cast<int>(hunk.c + hunk.d - 1);
		if (start <= end) {
			start = std::max(1, start);
			end = std::min(static_cast<int>(lines.size()), end);
			ranges.push_back({start, end});
		}
	}

	// Merge overlapping/adjacent ranges
	std::sort(ranges.begin(), ranges.end());
	std::vector<std::pair<int, int>> merged_ranges;
	for (const auto &r : ranges) {
		if (merged_ranges.empty()) {
			merged_ranges.push_back(r);
		} else {
			auto &last = merged_ranges.back();
			if (r.first <= last.second + 1) {
				last.second = std::max(last.second, r.second);
			} else {
				merged_ranges.push_back(r);
			}
		}
	}

	std::string result_msg = std::format("Successfully applied {} edits to {}\n\n", args_.edits.size(), args_.path);
	for (const auto &r : merged_ranges) {
		result_msg += std::format("[Modified Section lines {} - {}]:\n", r.first, r.second);
		for (int l = r.first; l <= r.second; ++l) {
			result_msg += std::format("{}: {}\n", l, lines[l - 1]);
		}
		result_msg += "\n";
	}

	return result_msg;
}

} // namespace tools
