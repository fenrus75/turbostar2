#include "clear_all_errors.h"
#include "../../build_error_manager.h"

namespace tools {

std::string clear_all_errors_tool::execute(agentlib::tool_context& ctx) {
    build_error_manager::get_instance().clear();
    return "All errors cleared successfully.";
}

} // namespace tools
