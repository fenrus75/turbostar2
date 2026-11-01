#include "vcd_signal_labels_tool.h"
#include "vcd_label_manager.h"
#include <format>
#include <sstream>

namespace tools
{

vcd_signal_labels_tool::vcd_signal_labels_tool(vcd_signal_labels_args args)
	: agentlib::llm_tool_action("Configuring VCD signal labels"), args_(std::move(args))
{
}

bool vcd_signal_labels_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_signal_labels_tool::execute(agentlib::tool_context &ctx)
{
	auto &mgr = verilog::vcd_label_manager::get_instance();

	bool file_saved = false;
	std::string msg;

	mgr.set_labels(args_.safe_path, args_.signal, args_.labels, args_.clear, file_saved, msg);

	std::ostringstream ss;
	ss << std::format("### VCD Signal Labels: `{}`\n\n", args_.requested_path);
	ss << msg << "\n\n";

	// Show current label table for this file
	auto all_labels = mgr.get_labels_for_file(args_.safe_path);
	if (!all_labels.empty()) {
		ss << "#### Active Label Mappings\n\n";
		for (const auto &[sig, val_map] : all_labels) {
			ss << std::format("- **Signal `{}`**:\n", sig);
			for (const auto &[val_key, label_str] : val_map) {
				ss << std::format("  - `{}` -> `{}`\n", val_key, label_str);
			}
		}
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
