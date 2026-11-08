#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace verilog
{

enum class endpoint_kind {
	register_cell,
	input_port,
	output_port,
	clock,
	constant,
	unconnected
};

enum class path_category {
	in_to_reg,
	reg_to_out,
	reg_to_reg,
	in_to_out
};

struct port_timing_info {
	std::string name;
	std::string direction; // "input" or "output"
	size_t bit_width{1};
	int layers{0}; // -1 if clock or unconnected
	endpoint_kind endpoint{endpoint_kind::unconnected};
	std::string endpoint_desc; // "Register", "Output", "Input", "Clock", "Constant", "Unconnected"
	std::string bound_signal_or_cell; // e.g. "value_reg" or "next_flags"
};

struct path_hop {
	std::string cell_name;
	std::string cell_type;
	std::string net_name;
	std::string src_location; // "file.sv:line"
};

struct timing_path {
	path_category category{path_category::reg_to_reg};
	size_t depth{0}; // layers of logic
	std::string startpoint;
	endpoint_kind start_kind{endpoint_kind::register_cell};
	std::string start_src;
	std::string endpoint;
	endpoint_kind end_kind{endpoint_kind::register_cell};
	std::string end_src;
	std::vector<path_hop> hops;
};

class netlist_graph_analyzer
{
      public:
	netlist_graph_analyzer() = default;

	// Load and parse JSON netlist (following untrusted data rules)
	bool load_from_file(std::string_view untrusted_path, std::string &out_error, std::string_view module_name = "");
	bool load_from_json_string(std::string_view untrusted_json, std::string &out_error, std::string_view module_name = "");
	bool load_from_json(const nlohmann::json &j, std::string &out_error, std::string_view module_name = "");

	// Queries
	const std::string &get_module_name() const noexcept { return module_name_; }
	const std::vector<port_timing_info> &get_input_ports() const noexcept { return input_ports_; }
	const std::vector<port_timing_info> &get_output_ports() const noexcept { return output_ports_; }
	size_t get_max_reg_to_reg_depth() const noexcept { return max_reg_to_reg_depth_; }

	// Extract worst paths across the module (sorted descending by depth)
	std::vector<timing_path> get_worst_paths(std::optional<path_category> category = std::nullopt, size_t top_k = 3) const;

	// Format sidecar markdown (.timing.md)
	std::string generate_timing_markdown(std::string_view target_mode = "Generic LUT4") const;

	// Helper to check if a cell type is a sequential element
	static bool is_sequential_cell_type(std::string_view type) noexcept;

	// Helper to check if a pin is a clock pin
	static bool is_clock_pin(std::string_view cell_type, std::string_view port_name) noexcept;

	// Helper to check if a pin is a sequential sink pin (D, E, R, S, etc.)
	static bool is_sequential_sink_pin(std::string_view cell_type, std::string_view port_name) noexcept;

	// Helper to check if a pin is a sequential source pin (Q, etc.)
	static bool is_sequential_source_pin(std::string_view cell_type, std::string_view port_name) noexcept;

      private:
	struct cell_pin_ref {
		size_t cell_idx{0};
		std::string port_name;
		size_t pin_idx{0};
	};

	struct internal_cell {
		std::string name;
		std::string type;
		std::string src_raw;
		bool is_sequential{false};
		std::map<std::string, std::vector<int>> input_conns;  // port -> bit IDs
		std::map<std::string, std::vector<int>> output_conns; // port -> bit IDs
		std::vector<int> all_input_bits;
		std::vector<int> all_output_bits;
	};

	struct bit_driver {
		enum class driver_type { primary_input, cell_q, cell_comb, constant, none };
		driver_type type{driver_type::none};
		std::string name; // port name or cell name
		std::string pin_name;
		std::string src;
		size_t cell_idx{0};
	};

	struct downstream_subpath {
		int depth{-1};
		std::string endpoint_name;
		endpoint_kind end_kind{endpoint_kind::unconnected};
		std::string end_src;
		std::vector<path_hop> hops;
	};

	struct downstream_cache_entry {
		downstream_subpath worst_reg;
		downstream_subpath worst_out;
		bool reaches_clock{false};
		bool computed{false};
	};

	std::string module_name_;
	std::vector<port_timing_info> input_ports_;
	std::vector<port_timing_info> output_ports_;
	size_t max_reg_to_reg_depth_{0};
	std::vector<timing_path> all_reg_to_reg_paths_;
	std::vector<timing_path> all_in_to_reg_paths_;
	std::vector<timing_path> all_reg_to_out_paths_;
	std::vector<timing_path> all_in_to_out_paths_;

	std::vector<internal_cell> cells_;
	std::map<int, bit_driver> bit_drivers_;
	std::map<int, std::vector<cell_pin_ref>> bit_loads_;
	std::map<int, std::vector<std::string>> bit_output_port_loads_; // bit_id -> output port names
	std::map<int, std::string> bit_to_net_name_;
	std::map<int, downstream_cache_entry> memo_downstream_;
	std::set<int> recursion_stack_;

	void build_graph(const nlohmann::json &mod_obj);
	void analyze_paths();
	downstream_cache_entry compute_downstream(int bit_id);
};

} // namespace verilog
