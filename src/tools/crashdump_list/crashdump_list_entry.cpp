#include "coredump_list.h"
#include "../../coredump_manager.h"

namespace tools {

bool coredump_list_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string coredump_list_tool::execute(agentlib::tool_context& /*ctx*/) {
    return coredump_manager::get_instance().get_markdown_table();
}

} // namespace tools