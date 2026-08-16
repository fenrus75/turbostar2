#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include "fs_utils.h"
#include "mime.h"
#include "fs_read_lines.h"
#include "codemap_utils.h"
#include "event_logger.h"

#include "agentlib/document_provider.h"
#include "agentlib/interactions/action.h"
#include "agentlib/virtual_file_system.h"

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

int determine_adjusted_end_line(int start, int requested_end, const std::vector<std::string> &lines, const std::string &path, agentlib::tool_context &ctx)
{
	int total_lines_read = static_cast<int>(lines.size());
	int file_end_line = start + total_lines_read - 1;

	// Rule 1: If we reached EOF within the extra 25 lines, return file_end_line.
	if (file_end_line < requested_end + 25) {
		return file_end_line;
	}

	// Rule 2: Fetch exact document symbols from central codemap infrastructure
	std::vector<codemap_symbol_info> symbols = get_document_codemap_symbols(path, ctx, 1);
	if (!symbols.empty()) {
		// Case A: If requested_end falls inside a symbol, extend to that symbol's end_line if <= requested_end + 25
		const codemap_symbol_info *enclosing_sym = nullptr;
		for (const auto &sym : symbols) {
			if (sym.start_line <= requested_end && sym.end_line > requested_end) {
				if (!enclosing_sym || (sym.end_line - sym.start_line < enclosing_sym->end_line - enclosing_sym->start_line)) {
					enclosing_sym = &sym;
				}
			}
		}

		if (enclosing_sym) {
			if (enclosing_sym->end_line <= requested_end + 25 && enclosing_sym->end_line <= file_end_line) {
				return enclosing_sym->end_line;
			}
		}

		// Case B: If a new symbol starts within [requested_end + 1, requested_end + 25], stop cleanly right before it!
		const codemap_symbol_info *next_sym = nullptr;
		for (const auto &sym : symbols) {
			if (sym.start_line > requested_end && sym.start_line <= requested_end + 25) {
				if (!next_sym || sym.start_line < next_sym->start_line) {
					next_sym = &sym;
				}
			}
		}

		if (next_sym) {
			int stop_line = next_sym->start_line - 1;
			if (stop_line >= requested_end && stop_line <= file_end_line) {
				return stop_line;
			}
		}
	}

	// Fallback heuristic for plain text / unstructured files without codemap symbols
	int requested_end_idx = requested_end - start;
	if (requested_end_idx >= 0 && requested_end_idx < total_lines_read) {
		auto trim = [](std::string_view s) -> std::string_view {
			size_t first = s.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos)
				return "";
			size_t last = s.find_last_not_of(" \t\r\n");
			return s.substr(first, last - first + 1);
		};

		for (int i = requested_end_idx + 1; i < total_lines_read; ++i) {
			std::string_view line = lines[i];
			std::string_view trimmed = trim(line);
			int current_line_num = start + i;

			if (trimmed == "}" || trimmed == "};" || trimmed == "]" || trimmed == ")" || trimmed == "end" || trimmed == "fi" ||
			    trimmed == "done") {
				return current_line_num;
			}
			if (trimmed.empty()) {
				return current_line_num - 1;
			}
		}
	}

	return requested_end;
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

	if (args_.tail.has_value()) {
		size_t total_lines = 0;
		if (args_.safe_path.find("://") != std::string::npos) {
			auto vfs = ctx.fs_security.get_vfs();
			if (vfs) {
				auto view_opt = vfs->read_file(args_.safe_path);
				if (view_opt) {
					std::string_view view = view_opt.value()->view();
					total_lines = std::count(view.begin(), view.end(), '\n');
					if (!view.empty() && view.back() != '\n') {
						total_lines++;
					}
				}
			}
		} else if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
			auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
			total_lines = doc_snapshot->get_line_count();
		} else {
			if (!fs_utils::is_regular_file(args_.safe_path)) {
				return "Error: Target is not a regular file.";
			}
			std::error_code ec;
			auto sz = std::filesystem::file_size(args_.safe_path, ec);
			if (ec || sz > 50 * 1024 * 1024) {
				return "Error: File is too large (>50MB) to read.";
			}
			std::ifstream file(args_.safe_path, std::ios::binary);
			if (file.is_open()) {
				total_lines = std::count(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), '\n');
				file.clear();
				file.seekg(0, std::ios::end);
				auto size = file.tellg();
				if (size > 0) {
					file.clear();
					file.seekg(-1, std::ios_base::end);
					char last_char;
					file.get(last_char);
					if (last_char != '\n') {
						total_lines++;
					}
				}
			}
		}

		int tail_val = *args_.tail;
		args_.start_line = std::max(1, static_cast<int>(total_lines) - tail_val + 1);
		args_.end_line = static_cast<int>(total_lines);
	}

	// Fallback bounds checks: clamp indices to valid positive ranges and prevent excessive reads
	// that could overwhelm the context window of the LLM.
	int start = std::max(1, args_.start_line);
	int requested_end = std::max(start, args_.end_line);

	if (requested_end - start > 50000) {
		requested_end = start + 50000;
	}

	// Always attempt to fetch up to 25 more lines to apply the semantic boundary heuristics.
	int fetch_end = requested_end + 25;

	file_read_result read_res;

	// Check if the path belongs to a Virtual File System (e.g. system:// or github:// schemes).
	if (args_.safe_path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			read_res = read_from_vfs(vfs, args_.safe_path, start, fetch_end);
		} else {
			read_res.success = false;
			read_res.error_message = "Error: Virtual file not found or not mounted.";
		}
	}
	// Read directly from local disk.
	else {
		read_res = read_from_disk(args_.safe_path, start, fetch_end);
	}

	int adjusted_end = requested_end;
	if (read_res.success && !read_res.lines.empty()) {
		adjusted_end = determine_adjusted_end_line(start, requested_end, read_res.lines, args_.safe_path, ctx);
		int keep_count = adjusted_end - start + 1;
		if (keep_count < 0) {
			keep_count = 0;
		}
		if (keep_count < static_cast<int>(read_res.lines.size())) {
			read_res.lines.resize(keep_count);
		}
	}

	// Store bounded range back to args so that all retrieval mechanisms share the same range values.
	args_.start_line = start;
	args_.end_line = adjusted_end;

	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_read_lines>(interaction_)) {
		custom_interaction->set_range(start, adjusted_end);
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
		std::string lang = mime::get_language_from_extension(args_.requested_path);

		std::stringstream ss;
		ss << std::format("Code for lines {} - {} of {} (total {} lines):\n{}{}\n", start, adjusted_end, args_.requested_path,
				  read_res.total_file_lines, fence, lang);
		int current_line = start;
		for (const auto &line : read_res.lines) {
			ss << std::format("{}: {}\n", current_line, line);
			current_line++;
		}
		ss << std::format("{}\n", fence);

		// Codemap integration rules:
		// Rule 1: If read_res reads whole implementation file (start == 1 && adjusted_end >= read_res.total_file_lines), skip codemap for this file.
		// Rule 2: If partial read and total file symbols < 10, append compact 3-column codemap.
		// Rule 3: If reading a header file (.h / .hpp), find matching implementation file (.cpp) and append its compact codemap.
		bool read_whole_file = (start == 1 && static_cast<size_t>(adjusted_end) >= read_res.total_file_lines);
		bool is_header = false;
		std::filesystem::path p(args_.safe_path);
		std::string ext = p.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
			is_header = true;
		}

		if (!read_whole_file) {
			auto symbols = get_document_codemap_symbols(args_.safe_path, ctx, /*min_lines=*/1);
			if (!symbols.empty()) {
				auto selection = select_prioritized_codemap_symbols(symbols, start, adjusted_end, args_.safe_path, ctx, /*max_items=*/10);
				if (!selection.selected_symbols.empty()) {
					event_logger::get_instance().log(
						std::format("fs_read_lines: path='{}', range={}-{}, codemap generated {} symbols across sections",
							args_.safe_path, start, adjusted_end, selection.selected_symbols.size()));
					ss << "\n" << format_codemap_table(args_.requested_path, selection.selected_symbols, /*rich_format=*/false, /*total_file_lines=*/0, selection.total_symbols, selection.omitted_count, &ctx);
				}
			}
		}

		if (is_header) {
			std::string matching_impl = find_matching_impl_file(args_.safe_path, ctx);
			if (!matching_impl.empty()) {
				auto impl_symbols = get_document_codemap_symbols(matching_impl, ctx, /*min_lines=*/1);
				if (!impl_symbols.empty()) {
					std::filesystem::path ip(matching_impl);
					auto selection = select_prioritized_codemap_symbols(impl_symbols, 1, 1000000, matching_impl, ctx, /*max_items=*/10);
					if (!selection.selected_symbols.empty()) {
						ss << "\n" << format_codemap_table(ip.filename().string(), selection.selected_symbols, /*rich_format=*/false, /*total_file_lines=*/0, selection.total_symbols, selection.omitted_count, &ctx);
					}
				}
			}
		}

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
	if (!fs_utils::is_regular_file(path)) {
		result.success = false;
		result.error_message = "Error: File is not a regular file (e.g. FIFO/device): " + path;
		return result;
	}

	struct stat sb;
	if (stat(path.c_str(), &sb) == -1) {
		result.success = false;
		result.error_message = "Error: File does not exist or cannot be accessed: " + path;
		return result;
	}

	if (!S_ISREG(sb.st_mode)) {
		result.success = false;
		result.error_message = "Error: Target path is not a regular file: " + path;
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

	file.clear();
	file.seekg(0);

	std::string line;
	int current_line = 1;

	while (current_line < start && std::getline(file, line)) {
		current_line++;
	}

	if (end >= start) {
		result.lines.reserve(end - start + 1);
	}

	while (current_line <= end && std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.size() > 4096) {
			line = line.substr(0, 4096) + "... (line truncated)";
		}

		// Sanitize ANSI escapes and control characters
		std::string clean_line;
		clean_line.reserve(line.size());
		for (size_t idx = 0; idx < line.size(); ++idx) {
			unsigned char c = static_cast<unsigned char>(line[idx]);
			if (c == 0x1b) {
				if (idx + 1 < line.size() && line[idx + 1] == '[') {
					idx += 2;
					while (idx < line.size() && (line[idx] < 0x40 || line[idx] > 0x7e)) {
						idx++;
					}
				}
				continue;
			}
			if (c < 32 && c != '\t') {
				// skip control chars
			} else if (c == 127) {
				// skip DEL
			} else {
				clean_line += c;
			}
		}

		result.lines.emplace_back(clean_line);
		current_line++;
	}

	result.success = true;
	return result;
}

} // namespace tools
