#include "fs_multi_replace_content.h"
#include "fs_replace_engine.h"
#include "fs_utils.h"
#include <algorithm>
#include <dtl/dtl.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../agentlib/file_health_utils.h"
#include "../../agentlib/interactions/base.h"
#include "../../markdown_utils.h"

namespace tools {

class interaction_fs_multi_replace_content : public agentlib::agent_interaction {
public:
	explicit interaction_fs_multi_replace_content(const std::string &path, size_t chunk_count) {
		call_text_ = std::format("Applying multi-chunk text replacement ({} chunks) to {}", chunk_count, path);
	}

	agentlib::interaction_type get_type() const override { return agentlib::interaction_type::action; }
	agentlib::interaction_role get_role() const override { return agentlib::interaction_role::agent; }

	bool needs_subpanel_header() const override { return true; }
	std::string get_subpanel_label() const override { return "Applying multi-chunk replacement"; }

	void set_result(const std::string &res) {
		result_text_ = res;
		invalidate_cache();
	}

	void set_target_type(const std::string &path, bool is_buffer) {
		(void)path;
		(void)is_buffer;
	}

	void set_diff(const std::vector<std::string> &before, const std::vector<std::string> &after) {
		dtl::Diff<std::string, std::vector<std::string>> d(before, after);
		d.compose();
		d.composeUnifiedHunks();

		std::stringstream ss;
		d.printUnifiedFormat(ss);

		std::string line;
		diff_lines_.clear();
		while (std::getline(ss, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			diff_lines_.push_back(line);
		}
		invalidate_cache();
	}

	std::string get_raw_text() const override {
		std::string raw = call_text_;
		if (!result_text_.empty()) {
			raw += "\nResult: " + result_text_;
		}
		for (const auto &dl : diff_lines_) {
			raw += "\n" + dl;
		}
		return raw;
	}

protected:
	std::vector<agentlib::interaction_line> format_lines(int width, agentlib::background_mode bg) const override {
		int label_color = get_color_pair(agentlib::interaction_role::thinking, bg);
		auto lines = wrap_text("", call_text_, width, label_color);

		if (!diff_lines_.empty()) {
			lines.push_back({std::string(std::min(width, 20), '-'), label_color});

			for (const auto &dl : diff_lines_) {
				int color = 3; // Default Yellow on Dark Blue
				if (dl.empty()) {
					lines.push_back({std::string(width, ' '), color});
					continue;
				}

				if (dl[0] == '-')
					color = 31; // Bright Red on Dark Blue
				else if (dl[0] == '+')
					color = 30; // Bright Green on Dark Blue
				else if (dl.length() > 2 && dl[0] == '@' && dl[1] == '@')
					color = 32; // Bright Cyan on Dark Blue

				auto dl_wrapped = wrap_text("", dl, width, color);
				lines.insert(lines.end(), dl_wrapped.begin(), dl_wrapped.end());
			}
		}

		if (!result_text_.empty()) {
			int res_color = get_color_pair(agentlib::interaction_role::agent, bg);
			if (result_text_.find("Successfully") != 0) {
				res_color = get_color_pair(agentlib::interaction_role::error, bg);
			}
			lines.push_back({"", res_color});
			auto res_lines = wrap_text("", "-> " + result_text_, width, res_color);
			lines.insert(lines.end(), res_lines.begin(), res_lines.end());
		}

		for (auto &line : lines) {
			int len = markdown_utils::display_width(line.text);
			if (len < width) {
				line.text += std::string(width - len, ' ');
			}
		}

		return lines;
	}

private:
	std::string call_text_;
	std::string result_text_;
	std::vector<std::string> diff_lines_;
};

fs_multi_replace_content_tool::fs_multi_replace_content_tool(fs_multi_replace_content_args args)
	: args_(std::move(args))
{
	interaction_ = std::make_shared<interaction_fs_multi_replace_content>(args_.path, args_.chunks.size());
}

std::shared_ptr<agentlib::agent_interaction> fs_multi_replace_content_tool::get_interaction() const
{
	return interaction_;
}

bool fs_multi_replace_content_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	std::string path_to_use = args_.safe_path;
	auto *vfs = ctx.fs_security.get_vfs();
	if (vfs && vfs->is_local_path_available(args_.safe_path)) {
		path_to_use = vfs->get_local_path(args_.safe_path);
	}
	if (!std::filesystem::exists(path_to_use)) {
		out_error = "Error: File does not exist. fs_multi_replace_content can only edit existing files.";
		return false;
	}
	if (args_.chunks.empty()) {
		out_error = "Error: Chunks list cannot be empty.";
		return false;
	}
	return true;
}

std::string fs_multi_replace_content_tool::execute(agentlib::tool_context &ctx)
{
	replace_engine_args engine_args;
	engine_args.path = args_.path;
	engine_args.safe_path = args_.safe_path;
	engine_args.strict = args_.strict;
	engine_args.chunks = args_.chunks;

	auto result = fs_replace_engine::execute(ctx, engine_args);

	if (!result.success) {
		if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_multi_replace_content>(interaction_)) {
			custom_interaction->set_result(result.error_message);
			if (ctx.trigger_ui_update) {
				ctx.trigger_ui_update();
			}
		}
		return result.error_message;
	}

	bool is_buffer = (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path) != nullptr);
	if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_multi_replace_content>(interaction_)) {
		custom_interaction->set_target_type(args_.path, is_buffer);
		custom_interaction->set_diff(result.before_lines, result.after_lines);
		custom_interaction->set_result(result.result_text);
		if (ctx.trigger_ui_update) {
			ctx.trigger_ui_update();
		}
	}

	return result.result_text;
}

} // namespace tools
