#include <filesystem>
#include "fs_file_size.h"
#include "fs_utils.h"

namespace tools
{

fs_file_size_tool::fs_file_size_tool(std::string safe_path)
    : llm_tool_action("Checking size of " + safe_path), safe_path_(std::move(safe_path))
{
}

bool fs_file_size_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_file_size_tool::execute(agentlib::tool_context &ctx)
{
	try {
		// VFS ROUTER: Check virtual file system if available
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs && (safe_path_.find("://") != std::string::npos || vfs->exists(safe_path_))) {
			auto info = vfs->get_file_info(safe_path_);
			if (info) {
				if (info->type == 'D') {
					set_failure(ctx, "Path is not a regular file");
					return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", "Error: The specified path is not a regular file (it may be a directory, device, or special file)");
				}
				set_success(ctx, std::to_string(info->size) + " bytes");
				return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", std::to_string(info->size) + " bytes");
			}
		}

		std::string resolved_path;
		std::string err;
		if (!ctx.fs_security.validate_access(safe_path_, agentlib::access_type::read, resolved_path, err)) {
			set_failure(ctx, err);
			return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", "Error: Access denied: " + err);
		}

		std::error_code ec;
		auto status = std::filesystem::status(resolved_path, ec);
		if (ec) {
			set_failure(ctx, ec.message());
			return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", "Error checking file status: " + ec.message());
		}

		if (!std::filesystem::is_regular_file(status)) {
			set_failure(ctx, "Path is not a regular file");
			return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", "Error: The specified path is not a regular file (it may be a directory, device, or special file)");
		}

		auto size = std::filesystem::file_size(resolved_path, ec);
		if (ec) {
			set_failure(ctx, ec.message());
			return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", "Error reading file size: " + ec.message());
		}
		set_success(ctx, std::to_string(size) + " bytes");
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", std::to_string(size) + " bytes");
	} catch (const std::exception &e) {
		set_failure(ctx, std::string(e.what()));
		return fs_utils::wrap_prompt_untrusted_data_tag("fs_file_size_result", "Error: " + std::string(e.what()));
	}
}

} // namespace tools
