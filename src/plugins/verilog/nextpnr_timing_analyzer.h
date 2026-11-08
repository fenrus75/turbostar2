#pragma once

#include "plugins/verilog/fpga_project_settings.h"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

namespace verilog {

/**
 * @brief Clock domain frequency and slack metrics parsed from nextpnr.
 */
struct clock_domain_timing {
	std::string domain_name;
	double constraint_mhz{0.0};
	double achieved_mhz{0.0};
	double slack_ns{0.0};
	bool met{true};
};

/**
 * @brief Physical hardware resource utilization parsed from nextpnr.
 */
struct device_resource_utilization {
	std::string resource_name;
	size_t used{0};
	size_t available{0};
	double percentage{0.0};
};

/**
 * @brief Categorization of timing path endpoints.
 */
enum class timing_path_type {
	reg_to_reg,
	in_to_reg,
	reg_to_out,
	in_to_out,
	unknown
};

/**
 * @brief Single hop in a physical critical path.
 */
struct timing_hop {
	std::string type;       // "clk-to-q", "routing", "logic", "setup", "source"
	double delay_ns{0.0};
	std::string from_cell;
	std::string from_port;
	std::string to_cell;
	std::string to_port;
	std::string net_name;

	// Correlated RTL locations & physical properties
	std::string cell_src_location;
	std::string cell_hint;
	std::string net_src_location;
	std::string clean_net;
	int distance{0};
	size_t fanout{0};
};

/**
 * @brief Analyzed physical critical path.
 */
struct timing_critical_path {
	timing_path_type path_type{timing_path_type::unknown};
	std::string startpoint;
	std::string start_src;
	std::string endpoint;
	std::string end_src;
	double total_delay_ns{0.0};
	double logic_delay_ns{0.0};
	double routing_delay_ns{0.0};
	double setup_delay_ns{0.0};
	double clk_to_q_delay_ns{0.0};
	std::vector<timing_hop> hops;
};

/**
 * @brief Grouped bottleneck entry for paths converging on the same endpoint register.
 */
struct timing_bottleneck_group {
	std::string endpoint;
	std::string end_src;
	double worst_delay_ns{0.0};
	double worst_logic_delay_ns{0.0};
	double worst_routing_delay_ns{0.0};
	timing_path_type path_type{timing_path_type::unknown};
	std::vector<std::string> sources;
	std::vector<timing_hop> representative_hops;
};

/**
 * @brief Analyzer for nextpnr place-and-route timing and utilization reports.
 *
 * Correlates physical cells and nets in timing.json against Yosys netlist JSON
 * to recover original SystemVerilog file:line anchors, computes logic vs routing
 * delay breakdowns, and pinpoints Reg -> Reg clock limiting bottlenecks.
 */
class nextpnr_timing_analyzer {
public:
	nextpnr_timing_analyzer() = default;

	// Load existing timing.json and optional netlist.json from file
	bool load_from_files(std::string_view timing_json_path, std::string_view netlist_json_path, std::string &out_error);

	// Load from JSON strings in memory
	bool load_from_json_strings(std::string_view timing_json_content, std::string_view netlist_json_content, std::string &out_error);

	// Synthesize RTL with Yosys and run nextpnr on demand
	bool synthesize_and_route(const fpga_target_config &cfg, std::span<const std::string> sources, std::string &out_error);

	// Access parsed metrics
	// Clean and normalize synthetic net names (e.g. cpu8008.A_reg.load_SB_LUT4... -> cpu8008.A_reg.load)
	static std::string normalize_net_name(std::string_view raw_net);

	// Get fanout (sink count) of a net or bit
	size_t get_net_fanout(std::string_view net_name) const;

	const std::vector<clock_domain_timing> &clock_domains() const { return clock_domains_; }
	const std::vector<device_resource_utilization> &utilization() const { return utilization_; }
	const std::vector<timing_critical_path> &critical_paths() const { return critical_paths_; }

	// Get Reg -> Reg critical paths (or all paths if filter_reg_to_reg is false)
	std::vector<timing_critical_path> get_filtered_paths(bool reg_to_reg_only = true, size_t top_k = 5) const;

	// Group paths by bottleneck sink endpoint
	std::vector<timing_bottleneck_group> get_sink_grouped_bottlenecks(bool reg_to_reg_only = true, size_t top_k = 5) const;

	// Format complete timing analysis report in Markdown
	std::string generate_timing_markdown(bool reg_to_reg_only = true, bool group_by_sink = true, size_t top_k = 5) const;

private:
	// Helpers for netlist correlation
	void parse_timing_json(const nlohmann::json &j);
	void parse_netlist_json(const nlohmann::json &j);
	void correlate_paths();

	std::string resolve_cell_src(std::string_view raw_cell, std::string &out_hint) const;
	std::string resolve_net_src(std::string_view raw_net) const;
	static std::string normalize_cell_name(std::string_view raw_cell);

	std::vector<clock_domain_timing> clock_domains_;
	std::vector<device_resource_utilization> utilization_;
	std::vector<timing_critical_path> critical_paths_;

	// Netlist cache for correlation
	struct cell_info {
		std::string type;
		std::string src_raw;
	};
	struct net_info {
		std::string src_raw;
		std::vector<int> bits;
	};
	std::string top_module_name_;
	std::map<std::string, cell_info> netlist_cells_;
	std::map<std::string, net_info> netlist_nets_;
	std::map<std::string, std::string> netlist_ports_; // port_name -> direction
	std::map<int, size_t> bit_sink_counts_;
	std::map<std::string, size_t> net_fanouts_;
};

} // namespace verilog
