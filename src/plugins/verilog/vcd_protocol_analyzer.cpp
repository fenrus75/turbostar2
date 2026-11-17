#include "plugins/verilog/vcd_protocol_analyzer.h"
#include "plugins/verilog/vcd_axi_protocol_checker.h"

#include <algorithm>
#include <format>
#include <sstream>

namespace verilog {

vcd_protocol_analyzer::vcd_protocol_analyzer()
{
	register_checker(std::make_unique<vcd_axi_protocol_checker>(axi_variant::axi_stream));
	register_checker(std::make_unique<vcd_axi_protocol_checker>(axi_variant::axi_lite));
	register_checker(std::make_unique<vcd_axi_protocol_checker>(axi_variant::axi4));
}

void vcd_protocol_analyzer::register_checker(std::unique_ptr<vcd_protocol_checker> checker)
{
	if (checker) {
		checkers_.push_back(std::move(checker));
	}
}

protocol_report vcd_protocol_analyzer::analyze(const vcd_parser &parser) const
{
	protocol_report report;
	for (const auto &checker : checkers_) {
		auto discovered = checker->discover_interfaces(parser);
		for (const auto &iface : discovered) {
			report.interfaces.push_back(checker->check(parser, iface));
		}
	}
	return report;
}

std::vector<const protocol_violation *> vcd_protocol_analyzer::find_violations_near_tick(const protocol_report &report,
											 size_t tick,
											 size_t window)
{
	std::vector<const protocol_violation *> matches;
	size_t lo = (tick > window) ? (tick - window) : 0;
	size_t hi = tick + window;
	for (const auto &iface : report.interfaces) {
		for (const auto &v : iface.violations) {
			if (v.cycle >= lo && v.cycle <= hi) {
				matches.push_back(&v);
			}
		}
	}
	std::sort(matches.begin(), matches.end(), [](const protocol_violation *a, const protocol_violation *b) {
		return a->cycle < b->cycle;
	});
	return matches;
}

std::string vcd_protocol_analyzer::format_interfaces_summary(const protocol_report &report)
{
	if (report.interfaces.empty()) {
		return "";
	}
	std::string out;
	for (size_t i = 0; i < report.interfaces.size(); ++i) {
		if (i > 0) {
			out += ", ";
		}
		const auto &s = report.interfaces[i];
		out += std::format("`{}` (`{}`, {} handshake(s), {} violation(s))",
				   s.iface.interface_name, s.iface.protocol, s.transactions, s.violations.size());
	}
	return out;
}

std::string vcd_protocol_analyzer::format_violations_markdown(const protocol_report &report,
							      std::string_view vcd_path,
							      bool is_cli_mode,
							      size_t max_show)
{
	size_t total = report.total_violations();
	if (total == 0) {
		return "";
	}

	std::ostringstream ss;
	ss << std::format("> [!WARNING] **Protocol Violations Detected ({} across {} interface(s))**\n",
			  total, report.interfaces.size());

	size_t shown = 0;
	const protocol_violation *first_v = nullptr;
	for (const auto &iface : report.interfaces) {
		for (const auto &v : iface.violations) {
			if (!first_v) {
				first_v = &v;
			}
			if (shown >= max_show) {
				continue;
			}
			ss << std::format("> - **{}** (`{}` on `{}` at tick {}): {}\n",
					  v.rule_id, v.protocol, v.interface_name, v.cycle, v.description);
			shown++;
		}
	}
	if (total > shown) {
		ss << std::format("> - *(and {} more violation(s))*\n", total - shown);
	}

	if (first_v && !vcd_path.empty() && !first_v->signals.empty()) {
		size_t w_start = (first_v->cycle > 2) ? (first_v->cycle - 2) : 0;
		size_t w_end = first_v->cycle + 3;
		if (is_cli_mode) {
			std::string sigs;
			for (size_t i = 0; i < first_v->signals.size(); ++i) {
				if (i > 0) sigs += ",";
				sigs += first_v->signals[i];
			}
			ss << std::format("> **Inspect**: `turbostar-verilog vcd-wave {} -s {} --start {} --end {}`\n",
					  vcd_path, sigs, w_start, w_end);
		} else {
			std::string sigs;
			for (size_t i = 0; i < first_v->signals.size(); ++i) {
				if (i > 0) sigs += ", ";
				sigs += std::format("\"{}\"", first_v->signals[i]);
			}
			ss << std::format("> **Inspect**: `vcd_wave(path=\"{}\", signals=[{}], start_tick={}, end_tick={})`\n",
					  vcd_path, sigs, w_start, w_end);
		}
	}

	return ss.str();
}

} // namespace verilog
