#include <filesystem>
#include "fs_utils.h"
#include "open_in_editor.h"

namespace tools
{

open_in_editor_tool::open_in_editor_tool(std::string safe_path)
    : llm_tool_action("Opening " + safe_path + " in editor"), safe_path_(std::move(safe_path))
{
}

bool open_in_editor_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string open_in_editor_tool::execute(agentlib::tool_context &ctx)
{
	try {
		std::error_code ec;

		// SECURITY CHECK: Verify the path exists and is a regular file (unless it's a VFS URI)
		if (safe_path_.find("://") == std::string::npos) {
			auto status = std::filesystem::status(safe_path_, ec);
			if (ec) {
				set_failure(ctx, ec.message());
				return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Error checking file status: " + ec.message());
			}

			if (!std::filesystem::exists(status)) {
				set_failure(ctx, "File does not exist");
				return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Error: File does not exist: " + safe_path_);
			}

			if (std::filesystem::is_directory(status)) {
				set_failure(ctx, "Path is a directory");
				return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Error: Path is a directory, not a regular file: " + safe_path_);
			}

			if (!std::filesystem::is_regular_file(status)) {
				set_failure(ctx, "Path is not a regular file");
				return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Error: The specified path is not a regular file (e.g. FIFO/device): " + safe_path_);
			}
		}

		if (!ctx.queue) {
			set_failure(ctx, "Editor event queue is not available");
			return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Error: Editor event queue is not available");
		}

		// Push the event to open the file to the editor TUI thread
		editor_event ev;
		ev.type = event_type::open_file;
		ev.payload = safe_path_;
		ctx.queue->push(ev);

		set_success(ctx, "File " + safe_path_ + " opened in editor");
		return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Successfully pushed open_file event for path: " + safe_path_);
	} catch (const std::exception &e) {
		set_failure(ctx, e.what());
		return fs_utils::wrap_prompt_untrusted_data_tag("open_in_editor_result", "Error opening file in editor: " + std::string(e.what()));
	}
}

} // namespace tools
