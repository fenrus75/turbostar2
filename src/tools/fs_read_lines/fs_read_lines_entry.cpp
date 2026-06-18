#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include "../../fs_utils.h"
#include "fs_read_lines.h"

#include "../../agentlib/document_provider.h"
#include "../../agentlib/interactions/action.h"
#include "../../agentlib/virtual_file_system.h"

namespace tools
{

namespace
{

size_t count_max_consecutive_backticks(const std::vector<std::string> &lines)
{
	size_t max_count = 0;
	for (const auto &line : lines) {
		size_t current_count = 0;
		for (char c : line) {
			if (c == '`') {
				current_count++;
				max_count = std::max(max_count, current_count);
			} else {
				current_count = 0;
			}
		}
	}
	return max_count;
}

std::string get_language_from_extension(const std::string &path)
{
	std::filesystem::path p(path);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
	if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".cc") {
		return "cpp";
	}
	if (ext == ".py") {
		return "python";
	}
	if (ext == ".json") {
		return "json";
	}
	if (ext == ".md") {
		return "markdown";
	}
	if (ext == ".sh" || ext == ".bash") {
		return "bash";
	}
	if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx") {
		return "javascript";
	}
	if (ext == ".html" || ext == ".htm") {
		return "html";
	}
	if (ext == ".css") {
		return "css";
	}
	if (ext == ".yaml" || ext == ".yml") {
		return "yaml";
	}
	if (ext == ".xml") {
		return "xml";
	}
	return "";
}

} // namespace

class interaction_fs_read_lines : public agentlib::interaction_action
{
      public:
	interaction_fs_read_lines(const std::string &path, int start, int end, size_t total)
	    : agentlib::interaction_action(path), path_(path), start_(start), end_(end), total_(total)
	{
		update_text();
	}

	agentlib::interaction_type get_type() const override
	{
		return agentlib::interaction_type::action;
	}
	agentlib::interaction_role get_role() const override
	{
		return agentlib::interaction_role::agent;
	}

	void set_total(size_t total)
	{
		total_ = total;
		update_text();
	}

	void set_range(int start, int end)
	{
		start_ = start;
		end_ = end;
		update_text();
	}

      private:
	void update_text()
	{
		if (total_ == 0) {
			set_action_text(path_ + " \xE2\x86\x92 Reading...");
			return;
		}

		int display_end = std::min<int>(end_, static_cast<int>(total_));

		if (start_ == 1 && end_ >= static_cast<int>(total_)) {
			set_action_text(path_ + " \xE2\x86\x92 Read whole file (" + std::to_string(total_) + " lines)");
		} else {
			set_action_text(path_ + " \xE2\x86\x92 Read lines " + std::to_string(start_) + "-" + std::to_string(display_end) +
					" of " + std::to_string(total_));
		}
	}

	std::string path_;
	int start_;
	int end_;
	size_t total_;
};

fs_read_lines_tool::fs_read_lines_tool(fs_read_lines_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<interaction_fs_read_lines>(args_.requested_path, args_.start_line, args_.end_line, 0);
}

std::shared_ptr<agentlib::agent_interaction> fs_read_lines_tool::get_interaction() const
{
	return interaction_;
}

bool fs_read_lines_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_read_lines_tool::execute(agentlib::tool_context &ctx)
{
	// Reset the drift tracker for this file since the LLM has read it.
	ctx.file_drift_tracker.erase(args_.safe_path);

	// Fallback bounds checks: clamp indices to valid positive ranges and prevent excessive reads
	// that could overwhelm the context window of the LLM.
	int start = std::max(1, args_.start_line);
	int end = std::max(start, args_.end_line);

	if (end - start > 50000) {
		end = start + 50000;
	}

	// Store bounded range back to args so that all retrieval mechanisms share the same range values.
	args_.start_line = start;
	args_.end_line = end;

	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
		custom_interaction->set_range(start, end);
	}

	file_read_result read_res;

	// Check if the path belongs to a Virtual File System (e.g. agent:// or github:// schemes).
	if (args_.safe_path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			read_res = read_from_vfs(vfs, args_.safe_path, start, end);
		} else {
			read_res.success = false;
			read_res.error_message = "Error: Virtual file not found or not mounted.";
		}
	}
	// Try reading from open document buffers in the editor, ensuring we reflect unsaved user modifications.
	else if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
		read_res = read_from_document(doc_snapshot.get(), start, end);
	}
	// Fall back to direct file read from local disk.
	else {
		read_res = read_from_disk(args_.safe_path, start, end);
	}

	// Propagate total file lines to the interaction UI for display on status logs.
	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
		custom_interaction->set_total(read_res.total_file_lines);
	}

	std::string result_text;

	// Handle read failures and empty lines result. Prepend line numbers to line text on success.
	if (!read_res.success) {
		result_text = read_res.error_message;
		if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
			custom_interaction->set_status(interaction_fs_read_lines::status::failure);
		}
	} else if (read_res.lines.empty()) {
		result_text = std::format("Requested line range is empty or past the end of the file. The file is {} lines long.",
					  read_res.total_file_lines);
		if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
			custom_interaction->set_status(interaction_fs_read_lines::status::failure);
		}
	} else {
		size_t max_backticks = count_max_consecutive_backticks(read_res.lines);
		size_t fence_len = std::max<size_t>(3, max_backticks + 1);
		std::string fence(fence_len, '`');
		std::string lang = get_language_from_extension(args_.requested_path);
		int end_line = start + static_cast<int>(read_res.lines.size()) - 1;

		std::stringstream ss;
		ss << std::format("Code for lines {} - {} of {}:\n{}{}\n", start, end_line, args_.requested_path, fence, lang);
		int current_line = start;
		for (const auto &line : read_res.lines) {
			ss << std::format("{}: {}\n", current_line, line);
			current_line++;
		}
		ss << std::format("{}\n", fence);
		result_text = ss.str();
		if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
			custom_interaction->set_status(interaction_fs_read_lines::status::success);
		}
	}

	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
		if (ctx.trigger_ui_update) {
			ctx.trigger_ui_update();
		}
	}

	return result_text;
}

// Retrieves lines from a mounted Virtual File System provider snapshot.
file_read_result fs_read_lines_tool::read_from_vfs(agentlib::virtual_file_system *vfs, const std::string &path, int start, int end) const
{
	file_read_result result;
	auto view_opt = vfs->read_file(path);
	if (!view_opt) {
		result.success = false;
		result.error_message = "Error: Virtual file not found or not mounted.";
		return result;
	}

	std::string_view view = view_opt.value()->view();

	// Calculate overall line count including any trailing content without a trailing newline.
	result.total_file_lines = std::count(view.begin(), view.end(), '\n');
	if (!view.empty() && view.back() != '\n') {
		result.total_file_lines++;
	}

	if (end >= start) {
		result.lines.reserve(end - start + 1);
	}

	int current_line = 1;
	size_t start_pos = 0;

	// Traverse the memory buffer segment by segment to extract lines within target bounds.
	while (start_pos < view.length()) {
		size_t end_pos = view.find('\n', start_pos);
		std::string_view line =
		    (end_pos == std::string_view::npos) ? view.substr(start_pos) : view.substr(start_pos, end_pos - start_pos);

		if (current_line >= start && current_line <= end) {
			result.lines.emplace_back(line);
		} else if (current_line > end) {
			break;
		}

		start_pos = (end_pos == std::string_view::npos) ? view.length() : end_pos + 1;
		current_line++;
	}

	result.success = true;
	return result;
}

// Retrieves lines from an active editor document's line buffer.
file_read_result fs_read_lines_tool::read_from_document(agentlib::document_snapshot *doc, int start, int end) const
{
	file_read_result result;
	result.total_file_lines = doc->get_line_count();

	int start_idx = start - 1;
	int end_idx = std::min<int>(end - 1, static_cast<int>(result.total_file_lines) - 1);

	// Validate start line bounds against document size.
	if (start_idx >= static_cast<int>(result.total_file_lines)) {
		result.success = false;
		result.error_message =
		    std::format("Requested start line is past the end of the file. The file is {} lines long.", result.total_file_lines);
		return result;
	}

	if (end_idx >= start_idx) {
		result.lines.reserve(end_idx - start_idx + 1);
	}

	for (int i = start_idx; i <= end_idx; ++i) {
		result.lines.emplace_back(doc->get_line_text(i));
	}

	result.success = true;
	return result;
}

// Retrieves lines directly from a file stored on the local disk.
file_read_result fs_read_lines_tool::read_from_disk(const std::string &path, int start, int end) const
{
	file_read_result result;
	struct stat sb;
	if (stat(path.c_str(), &sb) == -1) {
		result.success = false;
		result.error_message = "Error: File does not exist or cannot be accessed: " + path;
		return result;
	}

	// Safety check to avoid loading extremely large files (e.g. logs/databases) that could deplete RAM.
	if (sb.st_size > 50 * 1024 * 1024) {
		result.success = false;
		result.error_message = "Error: File is too large (>50MB) to read directly.";
		return result;
	}

	// Verify that the file does not contain binary patterns that are unsafe/unreadable as lines.
	if (fs_utils::is_binary_file(path)) {
		result.success = false;
		result.error_message = "Error: File appears to be binary. Cannot read text lines.";
		return result;
	}

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		result.success = false;
		result.error_message = "Error: Could not open file for reading.";
		return result;
	}

	// Fast line counting using standard buffer scan.
	result.total_file_lines = std::count(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), '\n');
	if (sb.st_size > 0) {
		file.clear();
		file.seekg(-1, std::ios_base::end);
		char last_char;
		file.get(last_char);
		if (last_char != '\n') {
			result.total_file_lines++;
		}
	}

	// Reset stream state and seek back to the beginning to start content extraction.
	file.clear();
	file.seekg(0);

	std::string line;
	int current_line = 1;

	// Fast-forward past lines preceding the requested range.
	while (current_line < start && std::getline(file, line)) {
		current_line++;
	}

	if (end >= start) {
		result.lines.reserve(end - start + 1);
	}

	// Read lines within requested range, trimming carriage returns for cross-platform robustness.
	while (current_line <= end && std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		result.lines.emplace_back(line);
		current_line++;
	}

	result.success = true;
	return result;
}

} // namespace tools
