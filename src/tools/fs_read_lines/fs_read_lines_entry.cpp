#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include "fs_read_lines.h"

#include "../../agentlib/interactions/action.h"

namespace tools
{

class interaction_fs_read_lines : public agentlib::interaction_action
{
      public:
	interaction_fs_read_lines(const std::string &path, int start, int end, size_t total)
	    : agentlib::interaction_action(path), path_(path), start_(start), end_(end), total_(total)
	{
		update_text();
	}

	agentlib::interaction_type get_type() const override { return agentlib::interaction_type::action; }
	agentlib::interaction_role get_role() const override { return agentlib::interaction_role::agent; }

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
	// 1. Fallback bounds checks (safeguards for LLM hallucinations)
	int start = std::max(1, args_.start_line);
	int end = std::max(start, args_.end_line);

	// Prevent reading massive blocks that blow out context window
	if (end - start > 2000) {
		end = start + 2000;
	}

	// Write them back to args_ so helper methods use the bounded values
	args_.start_line = start;
	args_.end_line = end;

	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
		custom_interaction->set_range(start, end);
	}

	// 2. Intercept VFS paths (skills://)
	if (args_.safe_path.starts_with("skills://")) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			auto view_opt = vfs->read_file(args_.safe_path);
			if (view_opt) {
				std::string_view view = view_opt.value();

				size_t total_lines = std::count(view.begin(), view.end(), '\n');
				if (!view.empty() && view.back() != '\n') {
					total_lines++;
				}

				if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
					custom_interaction->set_total(total_lines);
					// Defer status update until we know if it succeeded
				}

				// Very basic line slicing from string_view
				std::stringstream ss;
				int current_line = 1;
				size_t start_pos = 0;

				while (start_pos < view.length()) {
					size_t end_pos = view.find('\n', start_pos);
					std::string_view line = (end_pos == std::string_view::npos)
								    ? view.substr(start_pos)
								    : view.substr(start_pos, end_pos - start_pos);

					if (current_line >= args_.start_line && current_line <= args_.end_line) {
						ss << current_line << ": " << line << "\n";
					} else if (current_line > args_.end_line) {
						break;
					}

					start_pos = (end_pos == std::string_view::npos) ? view.length() : end_pos + 1;
					current_line++;
				}

				std::string result_text;
				if (ss.str().empty()) {
					result_text = "Requested line range is empty or past the end of the file.";
					if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
						custom_interaction->set_status(interaction_fs_read_lines::status::failure);
					}
				} else {
					result_text = ss.str();
					if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
						custom_interaction->set_status(interaction_fs_read_lines::status::success);
					}
				}

				if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
					if (ctx.trigger_ui_update)
						ctx.trigger_ui_update();
				}
				return result_text;
			}
		}

		if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
			custom_interaction->set_status(interaction_fs_read_lines::status::failure);
			if (ctx.trigger_ui_update)
				ctx.trigger_ui_update();
		}
		return "Error: Virtual file not found or not mounted.";
	}

	// 3. Try reading from active editor document first
	size_t total_lines = 0;
	std::string result_text;

	if (ctx.doc_provider) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
		if (doc_snapshot) {
			result_text = read_from_document(doc_snapshot.get(), total_lines);
			if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
				custom_interaction->set_total(total_lines);
				if (result_text.starts_with("Error:") || result_text.starts_with("Requested line range is empty") ||
				    result_text.starts_with("Requested start line is past")) {
					custom_interaction->set_status(interaction_fs_read_lines::status::failure);
				} else {
					custom_interaction->set_status(interaction_fs_read_lines::status::success);
				}
				if (ctx.trigger_ui_update)
					ctx.trigger_ui_update();
			}
			return result_text;
		}
	}

	// 4. Fallback to direct disk access
	result_text = read_from_disk(total_lines);
	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
		custom_interaction->set_total(total_lines);
		if (result_text.starts_with("Error:") || result_text.starts_with("Requested line range is empty")) {
			custom_interaction->set_status(interaction_fs_read_lines::status::failure);
		} else {
			custom_interaction->set_status(interaction_fs_read_lines::status::success);
		}
		if (ctx.trigger_ui_update)
			ctx.trigger_ui_update();
	}
	return result_text;
}

std::string fs_read_lines_tool::read_from_document(agentlib::document_snapshot *doc, size_t &out_total_lines) const
{
	std::stringstream ss;
	out_total_lines = doc->get_line_count();

	int start_idx = args_.start_line - 1;
	int end_idx = std::min<int>(args_.end_line - 1, static_cast<int>(out_total_lines) - 1);

	if (start_idx >= static_cast<int>(out_total_lines)) {
		return "Requested start line is past the end of the file.";
	}

	for (int i = start_idx; i <= end_idx; ++i) {
		ss << (i + 1) << ": " << doc->get_line_text(i) << "\n";
	}

	return ss.str();
}

std::string fs_read_lines_tool::read_from_disk(size_t &out_total_lines) const
{
	out_total_lines = 0;
	struct stat sb;
	if (stat(args_.safe_path.c_str(), &sb) == -1) {
		return "Error: File does not exist or cannot be accessed.";
	}

	// Skip excessively large files to prevent RAM exhaustion
	if (sb.st_size > 50 * 1024 * 1024) {
		return "Error: File is too large (>50MB) to read directly.";
	}

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		return "Error: Could not open file for reading.";
	}

	// Check for binary data
	char buffer[4096];
	file.read(buffer, sizeof(buffer));
	size_t bytes_read = file.gcount();
	if (memchr(buffer, '\0', bytes_read) != nullptr) {
		return "Error: File appears to be binary. Cannot read text lines.";
	}

	// Reset stream
	file.clear();
	file.seekg(0);

	// Fast count of total lines
	out_total_lines = std::count(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), '\n');
	if (sb.st_size > 0) {
		file.clear();
		file.seekg(-1, std::ios_base::end);
		char last_char;
		file.get(last_char);
		if (last_char != '\n') {
			out_total_lines++;
		}
	}

	// Reset stream for actual reading
	file.clear();
	file.seekg(0);

	std::stringstream ss;
	std::string line;
	int current_line = 1;

	// Discard lines until we reach start_line
	while (current_line < args_.start_line && std::getline(file, line)) {
		current_line++;
	}

	// Read and append requested lines
	while (current_line <= args_.end_line && std::getline(file, line)) {
		// Strip trailing \r if Windows format
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		ss << current_line << ": " << line << "\n";
		current_line++;
	}

	if (ss.str().empty()) {
		return "Requested line range is empty or past the end of the file.";
	}

	return ss.str();
}

} // namespace tools
