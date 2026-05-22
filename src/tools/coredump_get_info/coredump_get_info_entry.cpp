#include "coredump_get_info.h"
#include "../../coredump_manager.h"

namespace tools {

coredump_get_info_tool::coredump_get_info_tool(coredump_get_info_args args) : args_(std::move(args)) {}

bool coredump_get_info_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string coredump_get_info_tool::execute(agentlib::tool_context& /*ctx*/) {
    const auto& dumps = coredump_manager::get_instance().get_coredumps();
    for (const auto& dump : dumps) {
        if (dump.pid == args_.pid) {
            return dump.raw_info;
        }
    }
    return "Error: No coredump found with PID " + std::to_string(args_.pid);
}

} // namespace tools