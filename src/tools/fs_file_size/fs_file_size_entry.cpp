#include <filesystem>
#include "fs_file_size.h"

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
		std::error_code ec;

		// SECURITY CHECK: Verify the path points to a regular file
		// This prevents querying directories, devices, sockets, or other special files
		auto status = std::filesystem::status(safe_path_, ec);
		if (ec) {
			set_failure(ctx, ec.message());
			return "Error checking file status: " + ec.message();
		}

		if (!std::filesystem::is_regular_file(status)) {
			set_failure(ctx, "Path is not a regular file");
			return "Error: The specified path is not a regular file (it may be a directory, device, or special file)";
		}

		auto size = std::filesystem::file_size(safe_path_, ec);
		if (ec) {
			set_failure(ctx, ec.message());
			return "Error reading file size: " + ec.message();
		}
		set_success(ctx, std::to_string(size) + " bytes");
		return std::to_string(size) + " bytes";
	} catch (const std::exception &e) {
		set_failure(ctx, std::string(e.what()));
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
