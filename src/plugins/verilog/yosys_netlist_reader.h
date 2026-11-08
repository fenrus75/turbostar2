#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <nlohmann/json.hpp>

namespace verilog {

struct net_declaration {
	std::string hierarchical_name;
	std::string src_location; // e.g. "sv/register/register.sv:9"
	std::string role;         // "module output", "module input", "internal wire", etc.
};

struct cell_driver {
	std::string cell_name;
	std::string cell_type;
	std::string port_name;
	std::string src_location;
	std::string description; // e.g. "sequential register $adff port Q"
};

struct consumer_group {
	std::string module_prefix; // e.g. "cpu8008.alu"
	std::string file_path;
	std::vector<int> lines;    // e.g. [28, 30, 32, 33, 34]
	std::string description;  // e.g. "ALU operations ($add, $sub, $and)"
};

struct signal_source_trace {
	std::string matched_net_name;
	std::string query_signal;
	size_t bit_width{0};
	std::vector<int> bit_ids;
	bool is_top_port{false};
	std::string top_port_direction;

	std::vector<net_declaration> hierarchy_declarations;
	std::vector<cell_driver> drivers;
	std::vector<consumer_group> consumers;
};

class yosys_netlist_reader {
public:
	yosys_netlist_reader() = default;

	bool load_from_file(std::string_view netlist_path, std::string &out_error);
	bool load_from_json_string(std::string_view json_content, std::string &out_error);

	// Resolve a query signal (e.g. "tb_main.uut.cpu8008.A_val" or "A_val")
	std::optional<signal_source_trace> trace_signal(std::string_view query_signal) const;

	// Clean Yosys src string "file.sv:24.9-31.12" -> "file.sv:24-31" or "file.sv:24"
	static std::string clean_src_location(std::string_view raw_src);

	// Returns true if a location points to internal techmap or cell library sources (e.g. cells_map.v)
	static bool is_library_source(std::string_view s);

	// Clean and normalize synthetic net names (e.g. cpu8008.A_reg.load_SB_LUT4... -> cpu8008.A_reg.load)
	static std::string normalize_net_name(std::string_view raw_net);

	// Format source location for concise display (strips techmap line numbers e.g. cells_map.v:26-27 -> cells_map.v)
	static std::string clean_display_location(std::string_view loc);

	// Format trace as a token-efficient markdown block for LLMs
	static std::string format_trace_summary(const signal_source_trace &trace);

	size_t net_count() const { return netnames_.size(); }
	size_t cell_count() const { return cells_.size(); }

	// Returns true if the netlist appears to be post-synthesis / gate-level without hierarchical nets
	bool is_flat_gate_level() const;

private:
	struct parsed_net {
		std::string name;
		std::vector<int> bits;
		std::string src_raw;
		std::string hdlname;
	};

	struct parsed_cell {
		std::string name;
		std::string type;
		std::map<std::string, std::string> port_directions;
		std::map<std::string, std::vector<int>> connections;
		std::string src_raw;
	};

	std::string top_module_name_;
	std::map<std::string, std::string> top_ports_; // port_name -> direction
	std::map<std::string, parsed_net> netnames_;
	std::vector<parsed_cell> cells_;
};

} // namespace verilog
