#include <filesystem>
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

		// SECURITY CHECK: Verify the path exists and is a regular file
		auto status = std::filesystem::status(safe_path_, ec);
		if (ec) {
			set_failure(ctx, ec.message());
			return "Error checking file status: " + ec.message();
		}

		if (!std::filesystem::is_regular_file(status)) {
			set_failure(ctx, "Path is not a regular file");
			return "Error: The specified path is not a regular file (it may be a directory, device, or special file)";
		}

		if (!ctx.queue) {
			set_failure(ctx, "Editor event queue is not available");
			return "Error: Editor event queue is not available";
		}

		// Push the event to open the file to the editor TUI thread
		editor_event ev;
		ev.type = event_type::open_file;
		ev.payload = safe_path_;
		ctx.queue->push(ev);

		set_success(ctx, "File " + safe_path_ + " opened in editor");
		return "Successfully opened " + safe_path_ + " in the editor.";
	} catch (const std::exception &e) {
		set_failure(ctx, std::string(e.what()));
		return "Error: " + std::string(e.what());
	}
}

} // namespace tools
