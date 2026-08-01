#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "../../agentlib/document_provider.h"
#include "../../agentlib/virtual_file_system.h"
#include "../../fs_utils.h"
#include "mime.h"
#include "fs_list_dir.h"

namespace tools
{

fs_list_dir_tool::fs_list_dir_tool(fs_list_dir_args args)
    : llm_tool_action("Listing directory " + args.path), args_(std::move(args))
{
}

bool fs_list_dir_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (args_.path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs) {
			// Check if there are any mounts with this prefix
			auto listing = vfs->list_directory(args_.path);
			if (listing.empty() && !vfs->exists(args_.path)) {
				out_error = "Virtual directory not found or not mounted: " + args_.path;
				return false;
			}
			return true;
		}
		out_error = "Virtual file system not available.";
		return false;
	}

	if (!std::filesystem::is_directory(args_.path)) {
		out_error = "Path is not a directory: " + args_.path;
		return false;
	}
	return true;
}

std::string fs_list_dir_tool::execute(agentlib::tool_context &ctx)
{
	list_dir_result result;

	// Route scanning based on VFS URI vs local disk path.
	if (args_.path.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		if (!vfs) {
			set_failure(ctx, "VFS not available");
			return "Error: VFS not available.";
		}
		result = scan_vfs(vfs, args_.path);
	} else {
		result = scan_local_disk(args_.path, ctx);
	}

	if (!result.success) {
		set_failure(ctx, result.error_message);
		return result.error_message;
	}

	size_t total_items = result.entries.size();
	
	// Apply pagination slicing
	int start = args_.offset;
	int count = args_.limit;
	
	std::vector<dir_entry_metadata> sliced_entries;
	if (start < static_cast<int>(total_items)) {
		int end = std::min(start + count, static_cast<int>(total_items));
		sliced_entries.assign(result.entries.begin() + start, result.entries.begin() + end);
	}
	
	result.entries = std::move(sliced_entries);
	result.offset = start;
	result.limit = count;
	result.total_items = total_items;

	set_success(ctx, "Found " + std::to_string(total_items) + " items");
	return format_entries_table(result);
}

// Scans and retrieves file metadata for Virtual File System directories.
list_dir_result fs_list_dir_tool::scan_vfs(agentlib::virtual_file_system *vfs, const std::string &path) const
{
	list_dir_result result;
	result.success = true;

	std::string prefix = path;
	if (!prefix.ends_with('/')) {
		prefix += '/';
	}
	result.directory_name = prefix;

	auto entries = vfs->list_directory(prefix);
	// Stable sort VFS entries for deterministic pagination
	std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.uri < b.uri; });

	for (const auto &entry : entries) {
		std::string filename = entry.uri.substr(prefix.length());

		// Skip directory path self-references.
		if (filename.empty()) {
			continue;
		}

		// Filter out sub-directory grandchildren (report only direct children).
		size_t slash_pos = filename.find('/');
		if (slash_pos != std::string::npos) {
			if (slash_pos != filename.length() - 1) {
				continue;
			}
			filename = filename.substr(0, slash_pos);
		}

		dir_entry_metadata meta;
		meta.filename = filename;
		meta.type = entry.type;
		if (entry.type == 'F') {
			meta.size_bytes = std::to_string(entry.size);
			if (entry.size_in_lines > 0 || entry.size == 0) {
				meta.size_lines = std::to_string(entry.size_in_lines);
			}
			if (args_.rich_metadata) {
				if (!entry.details.empty()) {
					meta.details = entry.details;
				} else {
					meta.details = "Virtual Markdown document";
				}
				std::replace(meta.details.begin(), meta.details.end(), '|', ',');
			}
		}
		meta.permissions = "R--";
		result.entries.push_back(meta);
	}

	return result;
}

// Scans local disk directories, incorporating live size checks for files open in the editor.
list_dir_result fs_list_dir_tool::scan_local_disk(const std::string &path, agentlib::tool_context &ctx) const
{
	list_dir_result result;

	std::filesystem::path relative_path = std::filesystem::relative(path, ctx.fs_security.get_working_directory());
	std::string rel_str = relative_path.string();
	if (rel_str.empty() || rel_str == ".") {
		rel_str = "/ (Project Root)";
	}
	result.directory_name = rel_str;

	try {
		std::vector<std::filesystem::directory_entry> fs_entries;
		for (const auto &entry : std::filesystem::directory_iterator(path)) {
			fs_entries.push_back(entry);
		}

		// Alphabetical sort ensures stable, deterministic output arrays.
		std::sort(fs_entries.begin(), fs_entries.end(),
			  [](const auto &a, const auto &b) { return a.path().filename().string() < b.path().filename().string(); });

		for (const auto &entry : fs_entries) {
			std::string path_str = entry.path().string();
			std::string resolved_path;
			std::string error;

			// Strict visibility check: Only list files the LLM is allowed to read.
			if (!ctx.fs_security.validate_access(path_str, agentlib::access_type::read, resolved_path, error)) {
				continue;
			}

			dir_entry_metadata meta;
			meta.filename = entry.path().filename().string();

			if (entry.is_symlink()) {
				meta.type = 'L';
			} else if (entry.is_directory()) {
				meta.type = 'D';
			} else if (entry.is_regular_file()) {
				meta.type = 'F';

				// Check if the file is currently open in active editor buffers
				// to report the live line count and byte size including unsaved user modifications.
				bool read_from_editor = false;
				if (ctx.doc_provider) {
					auto doc_snapshot = ctx.doc_provider->get_open_document(resolved_path);
					if (doc_snapshot) {
						read_from_editor = true;
						size_t line_count = doc_snapshot->get_line_count();
						meta.size_lines = std::to_string(line_count);

						// Sum up characters in each line plus 1 byte for each newline character to get byte size.
						size_t total_bytes = 0;
						for (size_t i = 0; i < line_count; ++i) {
							total_bytes += doc_snapshot->get_line_text(i).length() + 1;
						}
						meta.size_bytes = std::to_string(total_bytes);
					}
				}

				if (!read_from_editor) {
					meta.size_bytes = std::to_string(entry.file_size());
					meta.size_lines = fs_utils::count_lines_in_file(resolved_path);
				}
			}

			auto p = entry.status().permissions();
			meta.permissions += (p & std::filesystem::perms::owner_read) != std::filesystem::perms::none ? "R" : "-";

			// Only report Write capability if the host OS permission is set AND the sandbox security manager allows it.
			bool os_can_write = (p & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
			bool agent_can_write = false;
			if (os_can_write) {
				std::string dump_path;
				std::string dump_err;
				agent_can_write =
				    ctx.fs_security.validate_access(path_str, agentlib::access_type::write, dump_path, dump_err);
			}
			meta.permissions += agent_can_write ? "W" : "-";
			meta.permissions += (p & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ? "X" : "-";

			// Inspect the file format and append file details.
			if (args_.rich_metadata && entry.is_regular_file()) {
				std::string desc = mime::detect_file_description(resolved_path);
				if (desc != "Unknown file type") {
					meta.details = desc;
					std::replace(meta.details.begin(), meta.details.end(), '|', ',');
				}
			}

			result.entries.push_back(meta);
		}
	} catch (const std::exception &e) {
		result.success = false;
		result.error_message = "Error reading directory: " + std::string(e.what());
		return result;
	}

	result.success = true;
	return result;
}

// Formats directory entry results into a unified Markdown table representation.
std::string fs_list_dir_tool::format_entries_table(const list_dir_result &result) const
{
	std::stringstream ss;
	if (args_.path.find("://") != std::string::npos) {
		ss << "# Virtual Directory " << result.directory_name << "\n\n";
	} else {
		ss << "# Directory " << result.directory_name << "\n\n";
	}

	int start_human = (result.total_items == 0) ? 0 : (result.offset + 1);
	int end_human = result.offset + result.entries.size();
	ss << "*Showing files " << start_human << " - " << end_human << " out of " << result.total_items << "*\n\n";

	if (end_human < static_cast<int>(result.total_items)) {
		ss << "*To view the next page, run `fs_list_dir` with offset=" << end_human << ".*\n\n";
	}

	if (args_.rich_metadata) {
		ss << "| Filename | File Type | File Size (bytes) | File Size (lines) | Permissions | Details |\n";
		ss << "| -------- | --------- | ----------------- | ----------------- | ----------- | ------- |\n";
	} else {
		ss << "| Filename | File Type | File Size (bytes) | File Size (lines) | Permissions |\n";
		ss << "| -------- | --------- | ----------------- | ----------------- | ----------- |\n";
	}

	for (const auto &meta : result.entries) {
		std::string type_str(1, meta.type);
		if (args_.rich_metadata) {
			ss << "| " << meta.filename << " | " << type_str << " | " << meta.size_bytes << " | " << meta.size_lines << " | "
			   << meta.permissions << " | " << meta.details << " |\n";
		} else {
			ss << "| " << meta.filename << " | " << type_str << " | " << meta.size_bytes << " | " << meta.size_lines << " | "
			   << meta.permissions << " |\n";
		}
	}

	return ss.str();
}

} // namespace tools
