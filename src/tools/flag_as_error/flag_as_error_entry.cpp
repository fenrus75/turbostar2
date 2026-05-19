#include "flag_as_error.h"
#include "../../build_error_manager.h"

namespace tools {

flag_as_error_tool::flag_as_error_tool(flag_as_error_args args) : args_(std::move(args)) {}

std::string flag_as_error_tool::execute(agentlib::tool_context& ctx) {
    build_error err;
    err.filepath = args_.safe_path;
    err.line = args_.line - 1; // 1-based to 0-based
    err.column = args_.column - 1; // 1-based to 0-based
    err.end_column = args_.end_column > 0 ? args_.end_column - 1 : 0;
    err.message = args_.error_string;
    err.is_warning = args_.is_warning;
    err.output_buffer_line = -1; // Not from compile window

    build_error_manager::get_instance().add_error(err);

    // Force a redraw to show the newly injected error overlay
    editor_event redraw_ev;
    redraw_ev.type = event_type::redraw;
    
    // We don't have direct access to global queue here, but the active window
    // will pick up the errors on its next render cycle.
    
    if (args_.is_warning) {
        return "Warning flagged at " + args_.safe_path + ":" + std::to_string(args_.line);
    } else {
        return "Error flagged at " + args_.safe_path + ":" + std::to_string(args_.line);
    }
}

} // namespace tools
