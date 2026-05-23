#include "fs_write_file.h"
#include <filesystem>
#include <fstream>
#include "../../agentlib/interactions/action.h"

namespace tools {

fs_write_file_tool::fs_write_file_tool(fs_write_file_args args) : args_(std::move(args)) {
    auto interaction = std::make_shared<agentlib::interaction_action>("Writing file " + args_.path);
    interaction->set_boxed(true, 5, args_.path);
    interaction_ = interaction;
}

std::shared_ptr<agentlib::agent_interaction> fs_write_file_tool::get_interaction() const {
    return interaction_;
}

bool fs_write_file_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    // 1. Open Document Check: Reject if the file is currently open in the editor.
    if (ctx.doc_provider) {
        auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
        if (doc_snapshot) {
            out_error = "Error: This file is currently open in the user's editor. You cannot overwrite an actively edited file. Please ask the user to close it.";
            return false;
        }
    }

    // 2. Existence Check
    if (std::filesystem::exists(args_.safe_path)) {
        if (!args_.force_overwrite) {
            out_error = "Error: File already exists. Set force_overwrite to true if you explicitly want to overwrite it.";
            return false;
        }
    }

    return true;
}

std::string fs_write_file_tool::execute(agentlib::tool_context& ctx) {
    auto custom_interaction = std::dynamic_pointer_cast<agentlib::interaction_action>(interaction_);
    
    try {
        std::ofstream out(args_.safe_path, std::ios::binary);
        if (!out.is_open()) {
            if (custom_interaction) custom_interaction->set_status(agentlib::interaction_action::status::failure, "Error: Could not open file for writing.");
            if (ctx.trigger_ui_update) ctx.trigger_ui_update();
            return "Error: Could not open file for writing.";
        }

        out << args_.content;
        out.close();

        if (custom_interaction) custom_interaction->set_status(agentlib::interaction_action::status::success);
        if (ctx.trigger_ui_update) ctx.trigger_ui_update();
        return "Successfully wrote to " + args_.path;
    } catch (const std::exception& e) {
        if (custom_interaction) custom_interaction->set_status(agentlib::interaction_action::status::failure, "Exception: " + std::string(e.what()));
        if (ctx.trigger_ui_update) ctx.trigger_ui_update();
        return "Error writing to file: " + std::string(e.what());
    }
}

} // namespace tools
