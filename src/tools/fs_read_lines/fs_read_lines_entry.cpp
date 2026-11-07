#include <algorithm>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include "codemap_utils.h"
#include "event_logger.h"
#include "fs_read_lines.h"
#include "fs_utils.h"
#include "mapped_file.h"
#include "mime.h"
#include "type_definition_cache.h"
#include "type_token_extractor.h"
#include "utf8.h"

#include "agentlib/document_provider.h"
#include "agentlib/interactions/action.h"
#include "agentlib/virtual_file_system.h"

namespace tools
{

namespace
{

size_t count_max_consecutive_backticks(std::span<const std::string_view> lines)
{
	size_t max_count = 0;
	for (std::string_view line : lines) {
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

int determine_adjusted_end_line(int start, int requested_end, std::span<const std::string_view> lines, const std::string &path,
				agentlib::tool_context &ctx)
{
	int total_lines_read = static_cast<int>(lines.size());
	int file_end_line = start + total_lines_read - 1;

	// If requested_end is at or beyond EOF, clamp to file_end_line.
	if (requested_end >= file_end_line) {
		return file_end_line;
	}

	// Fetch exact document symbols from central codemap infrastructure
	std::vector<codemap_symbol_info> symbols = get_document_codemap_symbols(path, ctx, 1);
	if (!symbols.empty()) {
		// If requested_end is already exactly at the end boundary of any symbol, do not extend!
		for (const auto &sym : symbols) {
			if (sym.end_line == requested_end) {
				return requested_end;
			}
		}

		// Case A: If requested_end falls inside a symbol, extend to that symbol's end_line if <= requested_end + 25
		const codemap_symbol_info *enclosing_sym = nullptr;
		for (const auto &sym : symbols) {
			if (sym.start_line <= requested_end && sym.end_line > requested_end) {
				if (!enclosing_sym ||
				    (sym.end_line - sym.start_line < enclosing_sym->end_line - enclosing_sym->start_line)) {
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

		// If requested_end is ALREADY at a closing delimiter, do not extend into subsequent blocks/functions!
		std::string_view end_line_trimmed = trim(lines[requested_end_idx]);
		if (end_line_trimmed == "}" || end_line_trimmed == "};" || end_line_trimmed.starts_with("} //") ||
		    end_line_trimmed.starts_with("} /*") || end_line_trimmed == "]" || end_line_trimmed == ")" ||
		    end_line_trimmed == "end" || end_line_trimmed == "fi" || end_line_trimmed == "done") {
			return requested_end;
		}

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

file_read_result read_from_vfs(agentlib::virtual_file_system *vfs, const std::string &path, int start, int end, std::optional<int> tail)
{
	file_read_result result;
	auto view_opt = vfs->read_file(path);
	if (!view_opt) {
		result.success = false;
		result.error_message = "Error: Virtual file not found or not mounted.";
		return result;
	}

	result.vfs_backing = std::move(*view_opt);
	std::string_view view = result.vfs_backing->view();

	// Calculate overall line count including any trailing content without a trailing newline.
	result.total_file_lines = std::count(view.begin(), view.end(), '\n');
	if (!view.empty() && view.back() != '\n') {
		result.total_file_lines++;
	}

	if (tail.has_value()) {
		start = std::max(1, static_cast<int>(result.total_file_lines) - *tail + 1);
		end = static_cast<int>(result.total_file_lines);
	}
	result.start_line = start;
	result.end_line = end;

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
			result.lines.push_back(line);
		} else if (current_line > end) {
			break;
		}

		start_pos = (end_pos == std::string_view::npos) ? view.length() : end_pos + 1;
		current_line++;
	}

	result.success = true;
	return result;
}

file_read_result read_from_document(agentlib::document_snapshot *doc, int start, int end, std::optional<int> tail)
{
	file_read_result result;
	result.total_file_lines = doc->get_line_count();

	if (tail.has_value()) {
		start = std::max(1, static_cast<int>(result.total_file_lines) - *tail + 1);
		end = static_cast<int>(result.total_file_lines);
	}
	result.start_line = start;
	result.end_line = end;

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
		size_t count = static_cast<size_t>(end_idx - start_idx + 1);
		result.owned_lines.reserve(count);
		result.lines.reserve(count);
	}

	for (int i = start_idx; i <= end_idx; ++i) {
		result.owned_lines.emplace_back(doc->get_line_text(i));
		result.lines.emplace_back(result.owned_lines.back());
	}

	result.success = true;
	return result;
}

file_read_result read_from_disk(const std::string &path, const std::string &requested_path, int start, int end, std::optional<int> tail)
{
	file_read_result result;
	std::string map_err;
	auto mf = mapped_file::get_cached(path, map_err, /*populate=*/true);
	if (!mf) {
		result.success = false;
		struct stat sb;
		if (stat(path.c_str(), &sb) == -1) {
			if (errno == ENOENT) {
				std::string alt = fs_utils::filename_suggest_alternative(requested_path);
				if (!alt.empty()) {
					result.error_message = std::format("Error: File does not exist: {}. Did you mean '{}'?", path, alt);
				} else {
					result.error_message = std::format("Error: File does not exist: {}", path);
				}
			} else {
				result.error_message = std::format("Error: File cannot be accessed ({}): {}", strerror(errno), path);
			}
			return result;
		}

		if (S_ISDIR(sb.st_mode)) {
			result.error_message = std::format("Error: Path is a directory, not a regular file: {}", path);
			return result;
		}

		if (!S_ISREG(sb.st_mode)) {
			result.error_message = std::format("Error: File is not a regular file (e.g. FIFO/device): {}", path);
			return result;
		}

		if (sb.st_size > 50 * 1024 * 1024) {
			result.error_message = "Error: File is too large (>50MB) to read directly.";
			return result;
		}

		result.error_message = "Error: Could not open file for reading.";
		return result;
	}

	// Safety check to avoid loading extremely large files (e.g. logs/databases) that could deplete RAM.
	if (mf->size() > 50 * 1024 * 1024) {
		result.success = false;
		result.error_message = "Error: File is too large (>50MB) to read directly.";
		return result;
	}

	// Verify that the file does not contain binary patterns that are unsafe/unreadable as lines.
	if (mf->is_binary(/*treat_null_as_binary=*/true)) {
		result.success = false;
		result.error_message = "Error: File appears to be binary. Cannot read text lines.";
		return result;
	}

	result.mapped_backing = mf;
	const bool clean_prefix = mf->has_clean_text_prefix();
	const char *file_base = reinterpret_cast<const char *>(mf->data());

	auto all_lines = mf->lines();
	result.total_file_lines = all_lines.size();

	if (tail.has_value()) {
		start = std::max(1, static_cast<int>(result.total_file_lines) - *tail + 1);
		end = static_cast<int>(result.total_file_lines);
	}
	result.start_line = start;
	result.end_line = end;

	if (end >= start && start >= 1 && static_cast<size_t>(start) <= all_lines.size()) {
		size_t first_idx = static_cast<size_t>(start - 1);
		size_t last_idx = std::min<size_t>(static_cast<size_t>(end), all_lines.size());
		result.lines.reserve(last_idx - first_idx);

		for (size_t i = first_idx; i < last_idx; ++i) {
			std::string_view raw_line = all_lines[i];
			// Fast path: if the first 4,096 bytes of the file were already verified clean during
			// `is_binary()` and this line ends within the first 4,096 bytes, no byte scan or copy is needed.
			if (clean_prefix && file_base != nullptr &&
			    static_cast<size_t>((raw_line.data() + raw_line.size()) - file_base) <= 4096) {
				result.lines.push_back(raw_line);
				continue;
			}

			bool truncated = false;
			if (raw_line.size() > 4096) {
				raw_line = raw_line.substr(0, 4096);
				truncated = true;
			}

			bool needs_sanitize = truncated;
			if (!needs_sanitize) {
				for (unsigned char c : raw_line) {
					if ((c < 32 && c != '\t') || c == 127) {
						needs_sanitize = true;
						break;
					}
				}
			}

			if (!needs_sanitize) {
				result.lines.push_back(raw_line);
				continue;
			}

			// Reserve owned_lines up front on the first dirty/truncated line so earlier string_views never dangle.
			if (result.owned_lines.empty()) {
				result.owned_lines.reserve(last_idx - first_idx);
			}

			// Sanitize ANSI escapes and control characters
			std::string clean_line;
			clean_line.reserve(raw_line.size() + (truncated ? 20 : 0));
			for (size_t idx = 0; idx < raw_line.size(); ++idx) {
				unsigned char c = static_cast<unsigned char>(raw_line[idx]);
				if (c == 0x1b) {
					if (idx + 1 < raw_line.size() && raw_line[idx + 1] == '[') {
						idx += 2;
						while (idx < raw_line.size() && (raw_line[idx] < 0x40 || raw_line[idx] > 0x7e)) {
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
					clean_line += static_cast<char>(c);
				}
			}
			if (truncated) {
				clean_line += "... (line truncated)";
			}

			result.owned_lines.emplace_back(std::move(clean_line));
			result.lines.emplace_back(result.owned_lines.back());
		}
	}

	result.success = true;
	return result;
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

file_read_result read_file_lines(const std::string &safe_path, const std::string &requested_path, int start_line, int end_line,
				 std::optional<int> tail, agentlib::tool_context &ctx, bool adjust_boundaries)
{
	// Reset the drift tracker for this file since the LLM has read it.
	ctx.file_drift_tracker.erase(safe_path);

	// Fallback bounds checks: clamp indices to valid positive ranges and prevent excessive reads
	// that could overwhelm the context window of the LLM.
	int start = std::max(1, start_line);
	int requested_end = std::max(start, end_line);

	if (requested_end - start > 50000) {
		requested_end = start + 50000;
	}

	// Always attempt to fetch up to 25 more lines to apply the semantic boundary heuristics (unless tail is requested or boundary adjustment disabled).
	int fetch_end = (tail.has_value() || !adjust_boundaries) ? requested_end : (requested_end + 25);

	file_read_result read_res;

	// Check if the path belongs to a Virtual File System (e.g. system:// or github:// schemes).
	if (safe_path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			read_res = read_from_vfs(vfs, safe_path, start, fetch_end, tail);
		} else {
			read_res.success = false;
			read_res.error_message = "Error: Virtual file not found or not mounted.";
		}
	}
	// Check if the file is currently open in the active editor buffer.
	else if (ctx.doc_provider && ctx.doc_provider->get_open_document(safe_path)) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(safe_path);
		read_res = read_from_document(doc_snapshot.get(), start, fetch_end, tail);
	}
	// Read directly from local disk.
	else {
		read_res = read_from_disk(safe_path, requested_path, start, fetch_end, tail);
	}

	if (tail.has_value()) {
		start = read_res.start_line;
		requested_end = read_res.end_line;
	}
	int adjusted_end = requested_end;
	if (adjust_boundaries && !tail.has_value() && read_res.success && !read_res.lines.empty()) {
		adjusted_end = determine_adjusted_end_line(start, requested_end, read_res.lines, safe_path, ctx);
	}
	int keep_count = adjusted_end - start + 1;
	if (keep_count < 0) {
		keep_count = 0;
	}
	if (keep_count < static_cast<int>(read_res.lines.size())) {
		read_res.lines.resize(keep_count);
	}

	if (!read_res.lines.empty() && read_res.owned_lines.empty() && (read_res.mapped_backing || read_res.vfs_backing)) {
		const char *begin_ptr = read_res.lines.front().data();
		const char *end_ptr = read_res.lines.back().data() + read_res.lines.back().size();
		if (begin_ptr != nullptr && end_ptr >= begin_ptr) {
			read_res.raw_range_view = std::string_view(begin_ptr, static_cast<size_t>(end_ptr - begin_ptr));
		}
	}

	read_res.start_line = start;
	read_res.end_line = adjusted_end;
	return read_res;
}

std::string format_file_lines_markdown(const std::string &requested_path, const file_read_result &read_res)
{
	if (!read_res.success) {
		return read_res.error_message;
	}
	if (read_res.lines.empty()) {
		return std::format("Requested line range is empty or past the end of the file. The file is {} lines long.",
				   read_res.total_file_lines);
	}

	size_t max_backticks = count_max_consecutive_backticks(read_res.lines);
	size_t fence_len = std::max<size_t>(3, max_backticks + 1);
	std::string fence(fence_len, '`');
	std::string lang = mime::get_language_from_extension(requested_path);

	size_t content_est = !read_res.raw_range_view.empty() ? read_res.raw_range_view.size() : (read_res.lines.size() * 64);
	std::string out;
	out.reserve(128 + requested_path.size() + content_est + read_res.lines.size() * 10);
	std::format_to(std::back_inserter(out), "Code for lines {} - {} of {} (total {} lines):\n{}{}\n", read_res.start_line,
		       read_res.end_line, requested_path, read_res.total_file_lines, fence, lang);

	char num_buf[16];
	int current_line = read_res.start_line;
	for (std::string_view line : read_res.lines) {
		auto [ptr, ec] = std::to_chars(num_buf, num_buf + sizeof(num_buf) - 2, current_line);
		ptr[0] = ':';
		ptr[1] = ' ';
		out.append(num_buf, static_cast<size_t>((ptr - num_buf) + 2));
		out.append(line);
		out.push_back('\n');
		current_line++;
	}
	out.append(fence);
	out.push_back('\n');
	return out;
}

std::string fs_read_lines_tool::execute(agentlib::tool_context &ctx)
{
	file_read_result read_res = read_file_lines(args_.safe_path, args_.requested_path, args_.start_line, args_.end_line, args_.tail,
						    ctx, /*adjust_boundaries=*/!args_.length.has_value());
	args_.start_line = read_res.start_line;
	args_.end_line = read_res.end_line;
	int start = args_.start_line;
	int adjusted_end = args_.end_line;

	// Early harvest: extract candidate type tokens from read range and dispatch async LSP queries
	std::vector<source_token> candidate_types;
	if (read_res.success && !read_res.lines.empty()) {
		if (!read_res.raw_range_view.empty()) {
			candidate_types = type_token_extractor::extract_candidates_from_source(args_.safe_path, read_res.raw_range_view, start);
		} else {
			candidate_types = type_token_extractor::extract_candidates(args_.safe_path, read_res.lines, start);
		}
		std::string cand_names;
		for (const auto &cand : candidate_types) {
			if (!cand_names.empty()) {
				cand_names += ", ";
			}
			cand_names += cand.name;
			if (!type_definition_cache::get_instance().contains(cand.name)) {
				type_definition_cache::get_instance().request_async(cand.name, args_.safe_path, cand.line - 1, cand.col);
			}
		}
		event_logger::get_instance().log(std::format("fs_read_lines: path='{}', range={}-{}, extracted {} candidate types: [{}]",
							     args_.safe_path, start, adjusted_end, candidate_types.size(), cand_names));
	}

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
		result_text = format_file_lines_markdown(args_.requested_path, read_res);

		// Codemap integration rules:
		// Rule 1: If read_res reads whole implementation file (start == 1 && adjusted_end >= read_res.total_file_lines), skip
		// codemap for this file. Rule 2: If partial read and total file symbols < 10, append compact 4-column codemap. Rule 3: If
		// reading a header file (.h / .hpp), find matching implementation file (.cpp) and append its compact 4-column codemap.
		bool read_whole_file = (start == 1 && static_cast<size_t>(adjusted_end) >= read_res.total_file_lines);
		bool is_header = false;
		std::filesystem::path p(args_.safe_path);
		std::string ext = utf8::to_lower(p.extension().string());
		if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
			is_header = true;
		}

		if (!is_header && !read_whole_file) {
			std::string class_preview = extract_class_context_preview(args_.safe_path, start, adjusted_end, read_res.lines, ctx,
										  read_res.raw_range_view);
			if (!class_preview.empty()) {
				result_text.push_back('\n');
				result_text.append(class_preview);
			}
		}

		size_t targets_surfaced = 0;

		if (!read_whole_file) {
			auto symbols = get_document_codemap_symbols(args_.safe_path, ctx, /*min_lines=*/1);
			if (!symbols.empty()) {
				auto selection = select_prioritized_codemap_symbols(symbols, start, adjusted_end, args_.safe_path, ctx,
										    /*max_items=*/10);
				if (!selection.selected_symbols.empty()) {
					targets_surfaced += selection.selected_symbols.size();
					event_logger::get_instance().log(std::format(
					    "fs_read_lines: path='{}', range={}-{}, codemap generated {} symbols across sections",
					    args_.safe_path, start, adjusted_end, selection.selected_symbols.size()));
					result_text.push_back('\n');
					result_text.append(format_codemap_table(args_.requested_path, selection.selected_symbols,
										/*total_file_lines=*/0, selection.total_symbols,
										selection.omitted_count, &ctx));
				}
			}
		}

		if (is_header) {
			std::string matching_impl = find_matching_impl_file(args_.safe_path, ctx);
			if (!matching_impl.empty()) {
				auto impl_symbols = get_document_codemap_symbols(matching_impl, ctx, /*min_lines=*/1);
				if (!impl_symbols.empty()) {
					std::filesystem::path ip(matching_impl);
					auto selection = select_prioritized_codemap_symbols(impl_symbols, 1, 1000000, matching_impl, ctx,
											    /*max_items=*/10);
					if (!selection.selected_symbols.empty()) {
						targets_surfaced += selection.selected_symbols.size();
						result_text.push_back('\n');
						result_text.append(format_codemap_table(ip.filename().string(), selection.selected_symbols,
											/*total_file_lines=*/0, selection.total_symbols,
											selection.omitted_count, &ctx));
					}
				}
			}
		}

		// Type Definitions: gather candidate types that have resolved in the cache
		std::string companion_header = is_header ? "" : find_matching_header_file(args_.safe_path, ctx);
		std::vector<type_definition_entry> resolved_types;
		size_t pending_count = 0;
		size_t excluded_file_count = 0;
		size_t already_reported_count = 0;
		size_t not_found_count = 0;
		for (const auto &cand : candidate_types) {
			auto entry = type_definition_cache::get_instance().lookup(cand.name);
			if (!entry) {
				not_found_count++;
				continue;
			}
			if (entry->state == type_cache_state::pending) {
				pending_count++;
				continue;
			}
			if (entry->state == type_cache_state::resolved) {
				// Exclude if defined in current file or its companion header
				if (entry->safe_file_path == args_.safe_path || entry->safe_file_path == companion_header) {
					excluded_file_count++;
					continue;
				}
				// Exclude if already reported in this agent session
				if (ctx.reported_type_definitions.find(entry->type_name) != ctx.reported_type_definitions.end()) {
					already_reported_count++;
					continue;
				}
				resolved_types.push_back(*entry);
				if (resolved_types.size() >= 5) {
					break;
				}
			}
		}

		event_logger::get_instance().log(std::format("fs_read_lines: path='{}', candidate_types={}, resolved={}, pending={}, "
							     "excluded_file={}, already_reported={}, not_found={}",
							     args_.safe_path, candidate_types.size(), resolved_types.size(), pending_count,
							     excluded_file_count, already_reported_count, not_found_count));

		if (!resolved_types.empty()) {
			targets_surfaced += resolved_types.size();
			result_text.append(type_definition_cache::format_type_definition_table(resolved_types));
			for (const auto &t : resolved_types) {
				ctx.reported_type_definitions.insert(t.type_name);
			}
		}

		if (targets_surfaced >= 2) {
			auto now = std::chrono::steady_clock::now();
			if (ctx.last_batch_read_tip_time.time_since_epoch().count() == 0 ||
			    now - ctx.last_batch_read_tip_time >= std::chrono::minutes(5)) {
				std::format_to(
				    std::back_inserter(result_text),
				    "\n*Tip: Fetch multiple symbols, line ranges, or class contexts from above in one turn using "
				    "fs_batch_read({}=[...]).*\n",
				    utf8::format_param_name("items"));
				ctx.last_batch_read_tip_time = now;
			}
		}

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

} // namespace tools
