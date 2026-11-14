#include "vcd_signal_qualifier_tool.h"
#include "vcd_label_manager.h"
#include "vcd_parser.h"
#include "vcd_parser_cache.h"
#include "plugins/verilog/fpga_project_settings.h"
#include <format>
#include <sstream>

namespace tools
{

vcd_signal_qualifier_tool::vcd_signal_qualifier_tool(vcd_signal_qualifier_args args)
	: agentlib::llm_tool_action("Managing VCD signal qualifiers"), args_(std::move(args))
{
}

bool vcd_signal_qualifier_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_signal_qualifier_tool::execute(agentlib::tool_context &ctx)
{
	auto &mgr = verilog::vcd_label_manager::get_instance();

	std::ostringstream ss;
	ss << std::format("### VCD Signal Qualifiers: `{}`\n\n", args_.requested_path);

	// Perform modification if qualifier, bindings, or clear provided
	if (!args_.qualifier.empty() || !args_.bindings.empty() || args_.clear) {
		bool file_saved = false;
		std::string msg;

		if (args_.clear) {
			mgr.set_qualifier(args_.safe_path, args_.signal, "", true, file_saved, msg, args_.global);
			ss << msg << "\n\n";
		} else if (!args_.bindings.empty()) {
			mgr.set_qualifiers_batch(args_.safe_path, args_.bindings, file_saved, msg, args_.global);
			ss << msg << "\n\n";
		} else if (!args_.signal.empty() && !args_.qualifier.empty()) {
			mgr.set_qualifier(args_.safe_path, args_.signal, args_.qualifier, false, file_saved, msg, args_.global);
			ss << msg << "\n";
			std::string hdr_err;
			auto parser_ptr = verilog::vcd_parser_cache::get_instance().get_or_parse(args_.safe_path, hdr_err);
			if (parser_ptr) {
				size_t matched_sig = 0;
				size_t matched_qual = 0;

				std::string clean_qual = args_.qualifier;
				if (clean_qual.starts_with('~') || clean_qual.starts_with('!')) {
					clean_qual.erase(0, 1);
				}

				for (const auto &sig : parser_ptr->get_signals()) {
					if (verilog::vcd_label_manager::signal_pattern_matches(args_.signal, sig.full_name)) {
						++matched_sig;
					}
					if (verilog::vcd_label_manager::signal_pattern_matches(clean_qual, sig.full_name)) {
						++matched_qual;
					}
				}

				if (matched_sig == 0) {
					if (verilog::fpga_project_settings::is_cli_mode()) {
						ss << std::format("> [!WARNING] Signal pattern `{}` currently matches **0 signals** in `{}`. "
						                  "Run `turbostar-verilog vcd-info {}` to confirm signal names.\n",
						                  args_.signal, args_.requested_path, args_.requested_path);
					} else {
						ss << std::format("> [!WARNING] Signal pattern `{}` currently matches **0 signals** in `{}`. "
						                  "Check `vcd_file_info` to confirm signal names.\n",
						                  args_.signal, args_.requested_path);
					}
				}
				if (matched_qual == 0) {
					if (verilog::fpga_project_settings::is_cli_mode()) {
						ss << std::format("> [!WARNING] Qualifier signal `{}` currently matches **0 signals** in `{}`. "
						                  "Run `turbostar-verilog vcd-info {}` to confirm signal names.\n",
						                  clean_qual, args_.requested_path, args_.requested_path);
					} else {
						ss << std::format("> [!WARNING] Qualifier signal `{}` currently matches **0 signals** in `{}`. "
						                  "Check `vcd_file_info` to confirm signal names.\n",
						                  clean_qual, args_.requested_path);
					}
				}
				if (matched_sig > 0 && matched_qual > 0) {
					ss << std::format("> [!NOTE] Bound `{}` (matches {} signal(s)) qualified by `{}` (matches {} signal(s)).\n",
					                  args_.signal, matched_sig, args_.qualifier, matched_qual);
				}
			}
			ss << "\n";
		}
	}

	// Show active qualifiers table
	auto all_quals = mgr.get_qualifiers_for_file(args_.safe_path);
	if (!all_quals.empty()) {
		ss << "#### Active Qualifier Bindings\n\n";
		ss << "| Signal Pattern | Qualifier Net | Polarity |\n";
		ss << "|:---------------|:--------------|:---------|\n";

		for (const auto &[sig, q_expr] : all_quals) {
			if (!args_.signal.empty() && args_.qualifier.empty() && args_.bindings.empty() && !args_.clear && sig != args_.signal) {
				continue;
			}
			bool active_low = (q_expr.starts_with('~') || q_expr.starts_with('!'));
			std::string net_name = q_expr;
			if (active_low) {
				net_name.erase(0, 1);
			}
			std::string polarity = active_low ? "Active-Low (`0` = valid)" : "Active-High (`1` = valid)";
			ss << std::format("| `{}` | `{}` | {} |\n", sig, net_name, polarity);
		}
	} else {
		ss << "*No signal qualifiers currently registered for this file.*\n";
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
