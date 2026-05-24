#include <filesystem>
#include <system_error>
#include "fs_mkdir.h"

namespace tools
{

fs_mkdir_tool::fs_mkdir_tool(std::string safe_path) : llm_tool_action("Creating directory " + safe_path), safe_path_(std::move(safe_path))
{
}

bool fs_mkdir_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (safe_path_.starts_with("skills://")) {
		out_error = "Cannot create directories in virtual file system.";
		return false;
	}
	return true;
}

std::string fs_mkdir_tool::execute(agentlib::tool_context &ctx)
{
	if (safe_path_.starts_with("skills://")) {
		set_failure(ctx, "VFS not writable");
		return "Error: Cannot create directories in virtual file system.";
	}

	try {
		std::error_code ec;
		bool created = std::filesystem::create_directories(safe_path_, ec);

		if (ec) {
			set_failure(ctx, ec.message());
			return "Failed to create directory: " + ec.message();
		}

		if (created) {
			set_success(ctx, "Directory created");
			return "Successfully created directory (and any parents if necessary): " + safe_path_;
		} else {
			set_success(ctx, "Directory already existed");
			return "Directory already exists: " + safe_path_;
		}
	} catch (const std::exception &e) {
		set_failure(ctx, std::string(e.what()));
		return "Exception creating directory: " + std::string(e.what());
	}
}

} // namespace tools
