#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace verilog
{

struct vcd_signal {
	std::string full_name;    // e.g. "tb_main.uut.cpu8008.opcode"
	std::string short_name;   // e.g. "opcode"
	std::string scope;        // e.g. "tb_main.uut.cpu8008"
	std::string id_code;      // e.g. ":"
	std::string var_type;     // e.g. "wire", "reg"
	int bit_width{1};
	std::string slice;        // e.g. "[7:0]"
};

struct vcd_clock_info {
	std::string signal_name;
	uint64_t period{0};
	uint64_t first_edge_time{0};
	size_t posedge_count{0};
	size_t negedge_count{0};
};

struct vcd_sample_point {
	size_t tick{0};
	uint64_t timestamp{0};
	std::string edge; // "posedge", "negedge", or ""
	std::map<std::string, std::string> signal_values; // keyed by requested signal name -> formatted string
	std::map<std::string, std::string> signal_raw_values; // keyed by requested signal name -> raw binary string
	std::map<std::string, std::string> signal_full_names; // keyed by requested signal name -> canonical full name
	size_t collapsed_count{0}; // >0 indicates a collapsed run of identical cycles
	size_t end_tick{0};        // End tick index of collapsed run
	uint64_t end_timestamp{0}; // End timestamp of collapsed run
};

class vcd_parser
{
      public:
	vcd_parser() = default;
	~vcd_parser() = default;

	// Parse header only (definitions, timescale, signals) - fast
	bool parse_header(std::string_view untrusted_path, std::string &out_error);

	// Parse full simulation data
	bool parse_all(std::string_view untrusted_path, std::string &out_error);

	// Metadata accessors
	const std::string &get_date() const noexcept { return date_; }
	const std::string &get_version() const noexcept { return version_; }
	const std::string &get_timescale() const noexcept { return timescale_; }
	uint64_t get_start_time() const noexcept { return start_time_; }
	uint64_t get_end_time() const noexcept { return end_time_; }
	const std::vector<vcd_signal> &get_signals() const noexcept { return signals_; }

	// Signal lookups with optional context scope preference for ambiguous suffix/leaf matches
	const vcd_signal *find_signal(std::string_view name_or_path, std::string_view preferred_scope = "") const;
	std::vector<const vcd_signal *> filter_signals(std::string_view scope_filter, std::string_view glob_pattern) const;

	// Clock analysis
	std::optional<vcd_clock_info> detect_clock() const;
	std::optional<vcd_clock_info> analyze_clock(std::string_view clock_signal_name) const;

	// Sample waveform data at clock edges
	// edge_mode: "posedge", "negedge", "both"
	std::vector<vcd_sample_point> sample_wave(
		std::string_view clock_signal,
		std::string_view edge_mode,
		const std::vector<std::string> &requested_signals,
		std::optional<size_t> start_tick,
		std::optional<size_t> end_tick,
		std::optional<uint64_t> start_time,
		std::optional<uint64_t> end_time,
		size_t max_rows,
		std::string &out_error,
		bool collapse_idle = false
	) const;

      private:
	std::string date_;
	std::string version_;
	std::string timescale_{"1ps"};
	uint64_t start_time_{0};
	uint64_t end_time_{0};
	bool header_parsed_{false};
	bool data_parsed_{false};
	std::string file_path_;

	std::vector<vcd_signal> signals_;
	std::map<std::string, std::vector<size_t>> id_to_signals_; // id_code -> list of indices in signals_
	std::map<std::string, size_t> name_to_signal_idx_;         // full_name -> index in signals_

	// Time-stamped raw value changes: id_code -> map of timestamp -> raw value string
	// (e.g. "0", "1", "00110110")
	std::map<std::string, std::map<uint64_t, std::string>> id_transitions_;
	std::vector<uint64_t> timestamps_;

	void parse_header_tokens(const std::string &content, std::string &out_error);
	void parse_body_tokens(const std::string &content);
};

} // namespace verilog
