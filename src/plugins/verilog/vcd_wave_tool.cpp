#include "vcd_wave_tool.h"
#include "vcd_parser.h"
#include <format>
#include <sstream>

namespace tools
{

vcd_wave_tool::vcd_wave_tool(vcd_wave_args args)
	: agentlib::llm_tool_action("Sampling VCD waveform"), args_(std::move(args))
{
}

bool vcd_wave_tool::validate_runtime(const agentlib::tool_context &/*ctx*/, std::string &/*out_error*/) const
{
	return true;
}

std::string vcd_wave_tool::execute(agentlib::tool_context &ctx)
{
	verilog::vcd_parser parser;
	std::string parse_err;

	if (!parser.parse_all(args_.safe_path, parse_err)) {
		set_failure(ctx, parse_err);
		return std::format("Error parsing VCD file: {}", parse_err);
	}

	std::string clock_name = args_.clock_signal;
	if (clock_name.empty()) {
		auto detected = parser.detect_clock();
		if (!detected) {
			set_failure(ctx, "No clock signal specified and failed to auto-detect clock");
			return "Error: No clock signal specified and failed to auto-detect a clock candidate in VCD.";
		}
		clock_name = detected->signal_name;
	}

	std::string sample_err;
	auto samples = parser.sample_wave(
		clock_name,
		args_.edge,
		args_.signals,
		args_.start_tick,
		args_.end_tick,
		args_.start_time,
		args_.end_time,
		args_.max_ticks,
		sample_err
	);

	if (!sample_err.empty()) {
		set_failure(ctx, sample_err);
		return std::format("Error sampling waveform: {}", sample_err);
	}

	std::ostringstream ss;
	ss << std::format("### VCD Waveform: `{}`\n", args_.requested_path);
	ss << std::format("Sampled on: `{}` ({}) | Timescale: {} | Total Samples: {}\n\n",
		clock_name, args_.edge, parser.get_timescale(), samples.size());

	if (samples.empty()) {
		ss << "*No clock transitions matched the requested tick/time window.*\n";
		set_success(ctx);
		return ss.str();
	}

	// Build markdown table header
	ss << "| Tick | Time (" << parser.get_timescale() << ") ";
	if (args_.edge == "both") {
		ss << "| Edge ";
	}
	for (const auto &sig : args_.signals) {
		ss << "| `" << sig << "` ";
	}
	ss << "|\n";

	// Alignments
	ss << "|:-----|:-------------------|";
	if (args_.edge == "both") {
		ss << ":----:|";
	}
	for (size_t i = 0; i < args_.signals.size(); ++i) {
		ss << ":------|";
	}
	ss << "\n";

	// Rows
	for (const auto &sample : samples) {
		ss << std::format("| {} | {} ", sample.tick, sample.timestamp);
		if (args_.edge == "both") {
			ss << std::format("| {} ", sample.edge == "posedge" ? "^" : "v");
		}
		for (const auto &sig : args_.signals) {
			auto val_it = sample.signal_values.find(sig);
			std::string val = (val_it != sample.signal_values.end()) ? val_it->second : "-";
			ss << std::format("| {} ", val);
		}
		ss << "|\n";
	}

	set_success(ctx);
	return ss.str();
}

} // namespace tools
