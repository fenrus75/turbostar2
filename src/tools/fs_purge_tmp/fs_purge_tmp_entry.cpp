#include "fs_purge_tmp.h"
#include "../../fs_utils.h"
#include <filesystem>
#include <vector>
#include <algorithm>

namespace tools
{

fs_purge_tmp_tool::fs_purge_tmp_tool(std::string substring)
    : llm_tool_action("Purging tmp:// scratch space"), substring_(std::move(substring))
{
}

bool fs_purge_tmp_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	auto vfs = ctx.fs_security.get_vfs();
	if (!vfs) {
		out_error = "Virtual file system not available.";
		return false;
	}
	return true;
}

std::string fs_purge_tmp_tool::execute(agentlib::tool_context &ctx)
{
	std::string tmp_dir_str = fs_utils::get_project_tmp_dir();
	std::string safe_tmp_dir;
	std::string sec_err;
	if (!ctx.fs_security.validate_access(tmp_dir_str, agentlib::access_type::write, safe_tmp_dir, sec_err)) {
		set_failure(ctx, "Access denied to project tmp directory: " + sec_err);
		return "Error: Access denied to project tmp directory: " + sec_err;
	}

	std::filesystem::path tmp_dir(safe_tmp_dir);
	std::error_code ec;
	if (!std::filesystem::exists(tmp_dir, ec)) {
		set_success(ctx, "Temp directory does not exist, nothing to purge.");
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_purge_tmp_result", "Temp directory does not exist, nothing to purge.");
	}

	std::string safe_prefix = tmp_dir.lexically_normal().string();
	if (!safe_prefix.empty() && safe_prefix.back() != '/') {
		safe_prefix += '/';
	}

	size_t deleted_files_count = 0;
	size_t deleted_dirs_count = 0;

	// Collect paths to delete
	std::vector<std::filesystem::path> paths_to_delete;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(tmp_dir, ec)) {
		std::string norm_path = entry.path().lexically_normal().string();
		if (!norm_path.starts_with(safe_prefix)) {
			continue; // Containment check
		}
		std::string filename = entry.path().filename().string();
		if (substring_.empty() || filename.find(substring_) != std::string::npos) {
			paths_to_delete.push_back(entry.path());
		}
	}

	// Sort paths in reverse order (longest paths first) to delete files/subdirectories inside a directory before the directory itself
	std::sort(paths_to_delete.begin(), paths_to_delete.end(), [](const auto &a, const auto &b) {
		return a.string().length() > b.string().length();
	});

	for (const auto &p : paths_to_delete) {
		if (std::filesystem::is_directory(p, ec)) {
			if (std::filesystem::remove(p, ec)) {
				deleted_dirs_count++;
			}
		} else {
			if (std::filesystem::remove(p, ec)) {
				deleted_files_count++;
			}
		}
	}

	std::string msg = "Successfully purged " + std::to_string(deleted_files_count) + " files and " +
			  std::to_string(deleted_dirs_count) + " directories from tmp scratch space.";
	set_success(ctx, msg);
	return fs_utils::wrap_prompt_untrusted_data_tag("fs_purge_tmp_result", msg);
}

} // namespace tools
