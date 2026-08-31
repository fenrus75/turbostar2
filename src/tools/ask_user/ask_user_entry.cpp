#include "ask_user.h"
#include "config_manager.h"
#include "fs_utils.h"

namespace tools
{

ask_user_tool::ask_user_tool(ask_user_args args) : args_(std::move(args))
{
}

bool ask_user_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.queue && !config_manager::get_instance().is_yolo_mode()) {
		out_error = "Execution Error: No event queue available to prompt the user.";
		return false;
	}
	return true;
}

std::string ask_user_tool::execute(agentlib::tool_context &ctx)
{
	if (config_manager::get_instance().is_yolo_mode()) {
		std::string auto_res = args_.options.empty() ? "Yes" : args_.options[0];
		return fs_utils::wrap_prompt_untrusted_data_tag("ask_user_result", auto_res);
	}

	if (!ctx.queue) {
		return fs_utils::wrap_prompt_untrusted_data_tag("ask_user_result", "Error: No event queue available to prompt the user.");
	}

	auto promise = std::make_shared<std::promise<std::string>>();
	auto future = promise->get_future();

	editor_event ev;
	ev.type = event_type::prompt_user;
	ev.payload = args_.question;
	ev.prompt_options = args_.options;
	ev.prompt_promise = promise;

	ctx.queue->push(ev);

	// Wait for the UI thread to resolve the promise
	try {
		std::string user_res = future.get();
		return fs_utils::wrap_prompt_untrusted_data_tag("ask_user_result", user_res);
	} catch (const std::exception &e) {
		return fs_utils::wrap_prompt_untrusted_data_tag("ask_user_result", std::string("Error: Failed to get user response - ") + e.what());
	}
}

} // namespace tools
