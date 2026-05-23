#include "clear_all_errors.h"
#include "../../build_error_manager.h"

namespace tools {

std::string clear_all_errors_tool::execute(agentlib::tool_context& ctx) {
    build_error_manager::get_instance().clear();
    set_success(ctx);
    return "All errors cleared successfully.";
}

} // namespace tools
