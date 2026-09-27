#include <format>
#include <sstream>
#include <string>
#include <vector>
#include "agentlib/interactions/action.h"
#include "agentlib/tool_context.h"
#include "codemap_utils.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "tools/fs_read_lines/fs_batch_read.h"
#include "tools/fs_read_lines/fs_read_lines.h"
#include "tools/fs_read_symbol/fs_read_symbol.h"

namespace tools
{

fs_batch_read_tool::fs_batch_read_tool(fs_batch_read_args args) : args_(std::move(args))
{
	size_t count = args_.items.size();
	std::string desc = std::format("Batch read {} item{}", count, count == 1 ? "" : "s");
	interaction_ = std::make_shared<agentlib::interaction_action>(std::move(desc));
}

std::shared_ptr<agentlib::agent_interaction> fs_batch_read_tool::get_interaction() const
{
	return interaction_;
}

bool fs_batch_read_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_batch_read_tool::execute_symbol(const batch_read_item &item, agentlib::tool_context &ctx)
{
	if (item.name.empty()) {
		return "Error: Parameter 'name' is required for type 'symbol'.";
	}

	fs_read_symbol_args sym_args;
	sym_args.requested_path = item.requested_path;
	sym_args.safe_path = item.safe_path;
	sym_args.symbol_name = item.name;

	fs_read_symbol_tool sym_tool(sym_args);
	std::string out_error;
	if (!sym_tool.validate_runtime(ctx, out_error)) {
		return std::format("Error: {}", out_error);
	}

	return sym_tool.execute(ctx);
}

std::string fs_batch_read_tool::execute_class_context(const batch_read_item &item, agentlib::tool_context &ctx)
{
	file_read_result read_res = read_file_lines(item.safe_path, item.requested_path, item.start_line, item.end_line, item.tail, ctx,
						    /*adjust_boundaries=*/false);
	if (!read_res.success) {
		return read_res.error_message;
	}

	std::string preview = extract_class_context_preview(item.safe_path, read_res.start_line, read_res.end_line, read_res.lines, ctx);
	if (!preview.empty()) {
		return preview;
	}

	if (!item.name.empty()) {
		return execute_symbol(item, ctx);
	}

	return std::format("No class context preview available for '{}'.", item.requested_path);
}

std::string fs_batch_read_tool::execute_item(const batch_read_item &item, agentlib::tool_context &ctx, bool is_single_item)
{
	if (item.has_error) {
		return item.error_message;
	}

	if (item.type == batch_item_type::lines) {
		if (is_single_item) {
			fs_read_lines_args r_args;
			r_args.requested_path = item.requested_path;
			r_args.safe_path = item.safe_path;
			r_args.start_line = item.start_line;
			r_args.end_line = item.end_line;
			r_args.tail = item.tail;
			r_args.length = item.length;

			fs_read_lines_tool r_tool(r_args);
			return r_tool.execute(ctx);
		}

		file_read_result read_res = read_file_lines(item.safe_path, item.requested_path, item.start_line, item.end_line, item.tail,
							    ctx, /*adjust_boundaries=*/true);
		return format_file_lines_markdown(item.requested_path, read_res);
	}

	if (item.type == batch_item_type::symbol) {
		return execute_symbol(item, ctx);
	}

	if (item.type == batch_item_type::class_context) {
		return execute_class_context(item, ctx);
	}

	return std::format("Error: Unknown batch read type for '{}'.", item.requested_path);
}

std::string fs_batch_read_tool::execute(agentlib::tool_context &ctx)
{
	if (args_.items.empty()) {
		return "Error: No items to read.";
	}

	if (args_.items.size() == 1) {
		std::string result = execute_item(args_.items[0], ctx, /*is_single_item=*/true);
		if (interaction_) {
			auto action = std::dynamic_pointer_cast<agentlib::interaction_action>(interaction_);
			if (action) {
				action->set_status(result.starts_with("Error:") ? agentlib::interaction_action::status::failure
										: agentlib::interaction_action::status::success);
			}
		}
		return result;
	}

	std::stringstream ss;
	size_t failures = 0;

	for (size_t i = 0; i < args_.items.size(); ++i) {
		const auto &item = args_.items[i];
		if (i > 0) {
			ss << "\n";
		}

		std::string type_desc;
		if (item.type == batch_item_type::lines) {
			if (item.tail.has_value()) {
				type_desc = std::format("tail {}", *item.tail);
			} else if (item.end_line >= 1000000) {
				type_desc = std::format("lines {}+", item.start_line);
			} else {
				type_desc = std::format("lines {}-{}", item.start_line, item.end_line);
			}
		} else if (item.type == batch_item_type::symbol) {
			type_desc = std::format("symbol: {}", item.name);
		} else if (item.type == batch_item_type::class_context) {
			type_desc = item.name.empty() ? "class_context" : std::format("class_context: {}", item.name);
		}

		ss << std::format("### [{}/{}] `{}` ({})\n", i + 1, args_.items.size(), item.requested_path, type_desc);

		std::string item_res = execute_item(item, ctx, /*is_single_item=*/false);
		if (item_res.starts_with("Error:")) {
			failures++;
		}
		ss << item_res;
		if (!item_res.ends_with('\n')) {
			ss << "\n";
		}
	}

	if (interaction_) {
		auto action = std::dynamic_pointer_cast<agentlib::interaction_action>(interaction_);
		if (action) {
			if (failures == args_.items.size()) {
				action->set_status(agentlib::interaction_action::status::failure);
			} else {
				action->set_status(agentlib::interaction_action::status::success);
			}
		}
	}

	return ss.str();
}

} // namespace tools
