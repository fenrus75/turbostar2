#include "create_code_review_item.h"
#include "fs_utils.h"
#include "agentlib/ai_agent.h"
#include "agentlib/interactions/action.h"
#include "codereview_manager.h"
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools
{

// Sanitize untrusted (LLM-supplied) text before it is interpolated into the parent agent's
// context.
static std::string sanitize_for_parent_line(const std::string &untrusted_text, size_t max_len = 200)
{
	std::string out;
	out.reserve(untrusted_text.size());
	for (unsigned char c : untrusted_text) {
		if (c < 32 || c == 127) {
			continue; // strip CR, LF, tab and all other C0 control characters
		}
		out.push_back(static_cast<char>(c));
	}
	if (out.size() > max_len) {
		out.resize(max_len);
		// Avoid truncating in the middle of a UTF-8 multi-byte sequence
		while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
			out.pop_back();
		}
	}
	return out;
}

create_code_review_item_tool::create_code_review_item_tool(create_code_review_item_args args)
    : llm_tool_action("Creating code review item for " + args.filename), args_(std::move(args))
{
	if (interaction_) {
		interaction_->set_boxed(true, 5, args_.filename);
	}
}

bool create_code_review_item_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string create_code_review_item_tool::execute(agentlib::tool_context &ctx)
{
	// 1. Resolve line content from file if line number was specified but no content was supplied.
	//    safe_path may be a plain disk path OR a VFS URI (tmp://, system://, github://, ...).
	if (args_.line_number > 0 && args_.line_number <= 50000 && args_.line_content.empty() && !args_.safe_path.empty()) {
		std::string line_content;
		if (args_.safe_path.find("://") != std::string::npos) {
			auto vfs = ctx.fs_security.get_vfs();
			if (vfs) {
				auto view_opt = vfs->read_file(args_.safe_path);
				if (view_opt) {
					std::string_view view = view_opt.value()->view();
					int current = 1;
					for (size_t start = 0; start <= view.size();) {
						size_t nl = view.find('\n', start);
						std::string_view ln = (nl == std::string_view::npos) ? view.substr(start) : view.substr(start, nl - start);
						if (current == args_.line_number) {
							line_content = std::string(ln);
							break;
						}
						if (nl == std::string_view::npos)
							break;
						current++;
						start = nl + 1;
					}
				}
			}
		} else {
			std::ifstream f(args_.safe_path);
			if (f.is_open()) {
				std::string line;
				int current = 1;
				while (std::getline(f, line)) {
					if (current == args_.line_number) {
						line_content = line;
						break;
					}
					current++;
				}
			}
		}
		if (!line_content.empty()) {
			args_.line_content = line_content;
		}
	}

	// 2. Create the code review item in the manager
	int item_id = codereview_manager::get_instance().create_code_review_item(
	    args_.summary, args_.filename, args_.line_number, args_.line_content, args_.severity, args_.description, args_.proposed_fix);

	// 3. Optional parent notification
	if (ctx.active_agent && !ctx.active_agent->is_suppress_parent_injection()) {
		auto parent = ctx.active_agent->get_parent();
		if (parent) {
			std::string safe_summary = sanitize_for_parent_line(args_.summary);
			std::string safe_filename = sanitize_for_parent_line(args_.filename);
			std::string parent_msg = std::format("Subagent created code review item #{} ({}): {}:{} - {}", item_id,
							     args_.severity, safe_filename, args_.line_number, safe_summary);
			parent->inject_context("user", parent_msg, false);
		}
	}

	// 4. Broadcast the codereview_updated event to the global event queue
	if (ctx.queue) {
		editor_event ev;
		ev.type = event_type::codereview_updated;
		ev.key_code = item_id;
		ctx.queue->push(ev);
	}

	set_success(ctx);

	nlohmann::json response_json = {{"id", item_id},
					{"status", "created"},
					{"filename", args_.filename},
					{"line_number", args_.line_number},
					{"line_content", args_.line_content}};
	return fs_utils::wrap_prompt_untrusted_data_tag("create_code_review_item_result", response_json.dump(2));
}

} // namespace tools
