#include "ask_user.h"

namespace tools {

ask_user_tool::ask_user_tool(ask_user_args args) : args_(std::move(args)) {}

bool ask_user_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string ask_user_tool::execute(agentlib::tool_context& ctx) {
    if (!ctx.queue) {
        return "Error: No event queue available to prompt the user.";
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
        return future.get();
    } catch (const std::exception& e) {
        return std::string("Error: Failed to get user response - ") + e.what();
    }
}

} // namespace tools
