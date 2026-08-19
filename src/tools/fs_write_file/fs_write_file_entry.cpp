#include <filesystem>
#include <fstream>
#include "../../agentlib/interactions/action.h"
#include "../../fs_utils.h"
#include "fs_write_file.h"


namespace tools
{

fs_write_file_tool::fs_write_file_tool(fs_write_file_args args) : args_(std::move(args))
{
	auto interaction = std::make_shared<agentlib::interaction_action>("Writing file " + args_.path);
	interaction->set_boxed(true, 5, args_.path);
	interaction_ = interaction;
}

std::shared_ptr<agentlib::agent_interaction> fs_write_file_tool::get_interaction() const
{
	return interaction_;
}

bool fs_write_file_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	// 1. Open Document Check: Reject if the file is currently open in the editor.
	if (ctx.doc_provider) {
		auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
		if (doc_snapshot) {
			out_error = "Error: This file is currently open in the user's editor. You cannot overwrite or append to an actively edited "
				    "file. Please ask the user to close it.";
			return false;
		}
	}



	return true;
}

std::string fs_write_file_tool::execute(agentlib::tool_context &ctx)
{
	// Reset the drift tracker for this file since the file is overwritten or modified
	ctx.file_drift_tracker.erase(args_.safe_path);

	auto custom_interaction = std::dynamic_pointer_cast<agentlib::interaction_action>(interaction_);

	try {
		bool is_vfs = args_.safe_path.find("://") != std::string::npos;
		if (is_vfs) {
			auto vfs = ctx.fs_security.get_vfs();
			if (!vfs) {
				if (custom_interaction)
					custom_interaction->set_status(agentlib::interaction_action::status::failure,
								       "Error: VFS is not initialized in security context.");
				if (ctx.trigger_ui_update)
					ctx.trigger_ui_update();
				return "Error: VFS is not initialized in security context.";
			}

			std::string final_content = args_.content;
			if (args_.append) {
				auto reader_opt = vfs->read_file(args_.safe_path);
				std::string existing;
				if (reader_opt.has_value()) {
					existing = std::string((*reader_opt)->view());
				}
				if (!existing.empty() && existing.back() != '\n') {
					existing += "\n";
				}
				final_content = existing + args_.content;
			}

			std::string desc = vfs->write_file(args_.safe_path, final_content.data(), final_content.size());
			if (desc.empty()) {
				if (custom_interaction)
					custom_interaction->set_status(agentlib::interaction_action::status::failure,
								       "Error writing to VFS path.");
				if (ctx.trigger_ui_update)
					ctx.trigger_ui_update();
				return "Error writing to VFS path: " + args_.path;
			}

			if (custom_interaction)
				custom_interaction->set_status(agentlib::interaction_action::status::success);
			if (ctx.trigger_ui_update)
				ctx.trigger_ui_update();

			return desc;
		}

		bool file_exists = std::filesystem::exists(args_.safe_path);
		std::string dir_note;
		if (!file_exists) {
			fs_utils::ensure_parent_directory_exists(args_.safe_path, dir_note);
		}

		bool inject_newline = false;

		// If appending and the file exists and is not empty, check the last byte
		if (args_.append && file_exists && std::filesystem::file_size(args_.safe_path) > 0) {
			std::ifstream in(args_.safe_path, std::ios::binary);
			if (in.is_open()) {
				in.seekg(-1, std::ios_base::end);
				char last_char;
				in.get(last_char);
				if (last_char != '\n') {
					inject_newline = true;
				}
				in.close();
			}
		}

		std::ios_base::openmode mode = std::ios::binary;
		if (args_.append) {
			mode |= std::ios::app;
		}

		std::ofstream out(args_.safe_path, mode);
		if (!out.is_open()) {
			if (custom_interaction)
				custom_interaction->set_status(agentlib::interaction_action::status::failure,
							       "Error: Could not open file for writing.");
			if (ctx.trigger_ui_update)
				ctx.trigger_ui_update();
			return "Error: Could not open file for writing.";
		}

		if (inject_newline) {
			out << "\n";
		}
		
		out << args_.content;
		out.close();

		if (custom_interaction)
			custom_interaction->set_status(agentlib::interaction_action::status::success);
		if (ctx.trigger_ui_update)
			ctx.trigger_ui_update();
		
		std::string res_msg;
		if (args_.append) {
			res_msg = "Successfully appended to " + args_.path;
		} else {
			res_msg = "Successfully wrote to " + args_.path;
		}

		if (!dir_note.empty()) {
			res_msg += "\n" + dir_note;
		}
		return res_msg;

	} catch (const std::exception &e) {
		if (custom_interaction)
			custom_interaction->set_status(agentlib::interaction_action::status::failure,
						       "Exception: " + std::string(e.what()));
		if (ctx.trigger_ui_update)
			ctx.trigger_ui_update();
		return "Error writing to file: " + std::string(e.what());
	}
}

} // namespace tools
