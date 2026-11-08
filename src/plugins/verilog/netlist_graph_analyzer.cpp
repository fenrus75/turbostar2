#include "plugins/verilog/netlist_graph_analyzer.h"
#include "plugins/verilog/yosys_netlist_reader.h"
#include "fs_utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <unistd.h>

namespace verilog
{

bool netlist_graph_analyzer::is_sequential_cell_type(std::string_view type) noexcept
{
	if (type.empty()) {
		return false;
	}
	if (type.starts_with("$_") &&
	    (type.find("FF") != std::string_view::npos || type.find("LATCH") != std::string_view::npos)) {
		return true;
	}
	if (type.starts_with("$dff") || type.starts_with("$adff") || type.starts_with("$sdff") ||
	    type.starts_with("$aldff") || type.starts_with("$dffe") || type.starts_with("$adffe") ||
	    type.starts_with("$sdffe") || type.starts_with("$dlatch") || type.starts_with("$adlatch") ||
	    type.starts_with("$sdffce") || type.starts_with("$ff")) {
		return true;
	}
	if (type.starts_with("SB_DFF")) {
		return true;
	}
	if (type.starts_with("FDRE") || type.starts_with("FDSE") || type.starts_with("FDCE") ||
	    type.starts_with("FDPE")) {
		return true;
	}
	if (type.starts_with("$mem") || type.find("RAM") != std::string_view::npos ||
	    type.find("BRAM") != std::string_view::npos || type.find("FIFO") != std::string_view::npos) {
		return true;
	}
	return false;
}

bool netlist_graph_analyzer::is_clock_pin(std::string_view /* cell_type */, std::string_view port_name) noexcept
{
	return port_name == "C" || port_name == "c" || port_name == "CLK" || port_name == "clk" ||
	       port_name == "clock" || port_name == "Clock" || port_name.ends_with("CLK") ||
	       port_name.ends_with("clk") || port_name.ends_with("_clock");
}

bool netlist_graph_analyzer::is_sequential_sink_pin(std::string_view cell_type, std::string_view port_name) noexcept
{
	if (is_clock_pin(cell_type, port_name)) {
		return false;
	}
	// Any input to a sequential cell that is not a clock pin is a timing sink (data, enable, reset, etc.)
	if (is_sequential_cell_type(cell_type)) {
		return true;
	}
	return port_name == "D" || port_name == "d" || port_name == "AD" || port_name == "D_IN" ||
	       port_name == "DATA" || port_name == "ENABLE" || port_name == "EN" || port_name == "E" ||
	       port_name == "RESET" || port_name == "R" || port_name == "SET" || port_name == "S" ||
	       port_name == "CLR" || port_name == "PRE" || port_name == "SR" || port_name == "CE" ||
	       port_name == "ARST" || port_name == "SRST" || port_name == "ALOAD";
}

bool netlist_graph_analyzer::is_sequential_source_pin(std::string_view cell_type, std::string_view port_name) noexcept
{
	if (!is_sequential_cell_type(cell_type)) {
		return false;
	}
	return port_name == "Q" || port_name == "q" || port_name == "DATA_OUT" || port_name == "Q_OUT" ||
	       port_name == "O" || port_name == "RD_DATA" || port_name == "RDATA" || port_name.starts_with("DO");
}

bool netlist_graph_analyzer::load_from_file(std::string_view untrusted_path, std::string &out_error, std::string_view module_name)
{
	std::string safe_path(untrusted_path);
	std::ifstream f(safe_path);
	if (!f.is_open()) {
		out_error = std::format("Failed to open file: {}", safe_path);
		return false;
	}

	try {
		nlohmann::json j = nlohmann::json::parse(f);
		return load_from_json(j, out_error, module_name);
	} catch (const std::exception &ex) {
		out_error = std::format("JSON parse error in {}: {}", safe_path, ex.what());
		return false;
	}
}

bool netlist_graph_analyzer::load_from_json_string(std::string_view untrusted_json, std::string &out_error, std::string_view module_name)
{
	try {
		nlohmann::json j = nlohmann::json::parse(untrusted_json);
		return load_from_json(j, out_error, module_name);
	} catch (const std::exception &ex) {
		out_error = std::format("JSON parse error: {}", ex.what());
		return false;
	}
}

bool netlist_graph_analyzer::load_from_json(const nlohmann::json &j, std::string &out_error, std::string_view module_name)
{
	if (!j.contains("modules") || !j["modules"].is_object()) {
		out_error = "Netlist JSON does not contain a 'modules' object";
		return false;
	}

	const auto &modules = j["modules"];
	if (modules.empty()) {
		out_error = "No modules found in netlist JSON";
		return false;
	}

	const nlohmann::json *chosen_mod = nullptr;
	std::string chosen_name;

	if (!module_name.empty()) {
		auto it = modules.find(std::string(module_name));
		if (it != modules.end() && it->is_object()) {
			chosen_mod = &(*it);
			chosen_name = std::string(module_name);
		} else {
			out_error = std::format("Module '{}' not found in netlist", module_name);
			return false;
		}
	} else {
		// Look for module with "top": 1 attribute
		for (const auto &[name, m] : modules.items()) {
			if (m.is_object() && m.contains("attributes") && m["attributes"].is_object()) {
				const auto &attrs = m["attributes"];
				if (attrs.contains("top")) {
					auto top_val = attrs["top"];
					if ((top_val.is_number() && top_val.get<int>() == 1) ||
					    (top_val.is_string() && top_val.get<std::string>() == "00000000000000000000000000000001")) {
						chosen_mod = &m;
						chosen_name = name;
						break;
					}
				}
			}
		}

		// Fallback to first module
		if (!chosen_mod) {
			auto it = modules.begin();
			chosen_mod = &(*it);
			chosen_name = it.key();
		}
	}

	module_name_ = chosen_name;
	build_graph(*chosen_mod);
	analyze_paths();
	return true;
}

void netlist_graph_analyzer::build_graph(const nlohmann::json &mod_obj)
{
	cells_.clear();
	bit_drivers_.clear();
	bit_loads_.clear();
	bit_output_port_loads_.clear();
	bit_to_net_name_.clear();
	input_ports_.clear();
	output_ports_.clear();
	memo_downstream_.clear();
	recursion_stack_.clear();
	memo_upstream_.clear();
	upstream_recursion_stack_.clear();
	all_reg_to_reg_paths_.clear();
	all_in_to_reg_paths_.clear();
	all_reg_to_out_paths_.clear();
	all_in_to_out_paths_.clear();
	max_reg_to_reg_depth_ = 0;

	// 1. Parse netnames to map bit IDs to human-readable names
	if (mod_obj.contains("netnames") && mod_obj["netnames"].is_object()) {
		for (const auto &[net_name, net_obj] : mod_obj["netnames"].items()) {
			if (net_obj.contains("bits") && net_obj["bits"].is_array()) {
				for (const auto &b : net_obj["bits"]) {
					if (b.is_number_integer()) {
						int bit_id = b.get<int>();
						if (!bit_to_net_name_.contains(bit_id)) {
							bit_to_net_name_[bit_id] = net_name;
						}
					}
				}
			}
		}
	}

	// 2. Parse primary module ports
	if (mod_obj.contains("ports") && mod_obj["ports"].is_object()) {
		for (const auto &[port_name, port_obj] : mod_obj["ports"].items()) {
			std::string dir = port_obj.value("direction", "input");
			std::vector<int> bits;
			if (port_obj.contains("bits") && port_obj["bits"].is_array()) {
				for (const auto &b : port_obj["bits"]) {
					if (b.is_number_integer()) {
						bits.push_back(b.get<int>());
					} else {
						bits.push_back(-1); // Constant
					}
				}
			}

			port_timing_info pinfo;
			pinfo.name = port_name;
			pinfo.direction = dir;
			pinfo.bit_width = bits.empty() ? 1 : bits.size();
			pinfo.bit_ids = bits;

			if (dir == "input") {
				input_ports_.push_back(pinfo);
				for (size_t i = 0; i < bits.size(); ++i) {
					int bit_id = bits[i];
					if (bit_id > 0) {
						bit_driver drv;
						drv.type = bit_driver::driver_type::primary_input;
						drv.name = port_name;
						drv.pin_name = std::format("{}[{}]", port_name, i);
						bit_drivers_[bit_id] = std::move(drv);
					}
				}
			} else if (dir == "output") {
				output_ports_.push_back(pinfo);
				for (int bit_id : bits) {
					if (bit_id > 0) {
						bit_output_port_loads_[bit_id].push_back(port_name);
					}
				}
			}
		}
	}

	// 3. Parse cells
	if (mod_obj.contains("cells") && mod_obj["cells"].is_object()) {
		for (const auto &[cell_name, cell_obj] : mod_obj["cells"].items()) {
			internal_cell c;
			c.name = cell_name;
			c.type = cell_obj.value("type", "");
			c.is_sequential = is_sequential_cell_type(c.type);

			if (cell_obj.contains("attributes") && cell_obj["attributes"].is_object()) {
				c.src_raw = cell_obj["attributes"].value("src", "");
			}

			std::map<std::string, std::string> port_dirs;
			if (cell_obj.contains("port_directions") && cell_obj["port_directions"].is_object()) {
				for (const auto &[pname, pdir] : cell_obj["port_directions"].items()) {
					if (pdir.is_string()) {
						port_dirs[pname] = pdir.get<std::string>();
					}
				}
			}

			if (cell_obj.contains("connections") && cell_obj["connections"].is_object()) {
				for (const auto &[pname, conn_arr] : cell_obj["connections"].items()) {
					if (!conn_arr.is_array()) {
						continue;
					}
					std::vector<int> con_bits;
					for (const auto &b : conn_arr) {
						if (b.is_number_integer()) {
							con_bits.push_back(b.get<int>());
						} else {
							con_bits.push_back(-1);
						}
					}

					auto it_pdir = port_dirs.find(pname);
					std::string pdir = (it_pdir != port_dirs.end()) ? it_pdir->second : "";
					if (pdir.empty()) {
						// Infer direction by pin name conventions
						if (pname == "Q" || pname == "q" || pname == "Y" || pname == "y" || pname == "O" ||
						    pname == "CO" || pname == "COUT") {
							pdir = "output";
						} else {
							pdir = "input";
						}
					}

					if (pdir == "output") {
						c.output_conns[pname] = con_bits;
						for (int b : con_bits) {
							if (b > 0) {
								c.all_output_bits.push_back(b);
							}
						}
					} else {
						c.input_conns[pname] = con_bits;
						for (int b : con_bits) {
							if (b > 0) {
								c.all_input_bits.push_back(b);
							}
						}
					}
				}
			}

			size_t cell_idx = cells_.size();
			cells_.push_back(std::move(c));

			const auto &stored_cell = cells_.back();

			// Register output drivers for each bit driven by this cell
			for (const auto &[pname, con_bits] : stored_cell.output_conns) {
				for (size_t pin_idx = 0; pin_idx < con_bits.size(); ++pin_idx) {
					int bit_id = con_bits[pin_idx];
					if (bit_id > 0) {
						bit_driver drv;
						drv.name = stored_cell.name;
						drv.pin_name = pname;
						drv.src = yosys_netlist_reader::clean_src_location(stored_cell.src_raw);
						drv.cell_idx = cell_idx;
						if (stored_cell.is_sequential) {
							if (is_sequential_source_pin(stored_cell.type, pname)) {
								drv.type = bit_driver::driver_type::cell_q;
							} else {
								drv.type = bit_driver::driver_type::cell_comb;
							}
						} else {
							drv.type = bit_driver::driver_type::cell_comb;
						}
						bit_drivers_[bit_id] = std::move(drv);
					}
				}
			}

			// Register loads for each bit consumed by this cell
			for (const auto &[pname, con_bits] : stored_cell.input_conns) {
				for (size_t pin_idx = 0; pin_idx < con_bits.size(); ++pin_idx) {
					int bit_id = con_bits[pin_idx];
					if (bit_id > 0) {
						cell_pin_ref ref;
						ref.cell_idx = cell_idx;
						ref.port_name = pname;
						ref.pin_idx = pin_idx;
						bit_loads_[bit_id].push_back(std::move(ref));
					}
				}
			}
		}
	}
}

netlist_graph_analyzer::downstream_cache_entry netlist_graph_analyzer::compute_downstream(int bit_id)
{
	if (bit_id <= 0) {
		return {};
	}

	auto it = memo_downstream_.find(bit_id);
	if (it != memo_downstream_.end() && it->second.computed) {
		return it->second;
	}

	if (recursion_stack_.contains(bit_id)) {
		// Combinational loop detected; break cycle gracefully
		downstream_cache_entry loop_res;
		loop_res.computed = true;
		return loop_res;
	}

	recursion_stack_.insert(bit_id);

	downstream_cache_entry res;
	res.computed = true;

	// Check direct output port loads
	auto out_it = bit_output_port_loads_.find(bit_id);
	if (out_it != bit_output_port_loads_.end()) {
		for (const auto &out_port : out_it->second) {
			if (0 > res.worst_out.depth) {
				res.worst_out.depth = 0;
				res.worst_out.endpoint_name = out_port;
				res.worst_out.end_kind = endpoint_kind::output_port;
			}
		}
	}

	// Check cell pin loads
	auto load_it = bit_loads_.find(bit_id);
	if (load_it != bit_loads_.end()) {
		for (const auto &ref : load_it->second) {
			if (ref.cell_idx >= cells_.size()) {
				continue;
			}
			const auto &c = cells_[ref.cell_idx];

			if (c.is_sequential) {
				if (is_clock_pin(c.type, ref.port_name)) {
					res.reaches_clock = true;
				} else if (is_sequential_sink_pin(c.type, ref.port_name)) {
					// Sequential data/enable/reset sink reached
					if (0 > res.worst_reg.depth) {
						res.worst_reg.depth = 0;
						res.worst_reg.endpoint_name = std::format("{}.{}", c.name, ref.port_name);
						res.worst_reg.end_kind = endpoint_kind::register_cell;
						res.worst_reg.end_src = yosys_netlist_reader::clean_src_location(c.src_raw);
					}
				}
			} else {
				// Combinational cell: propagate forward through all output bits of c
				path_hop hop;
				hop.cell_name = c.name;
				hop.cell_type = c.type;
				hop.net_name = bit_to_net_name_.contains(bit_id) ? bit_to_net_name_[bit_id] : std::format("wire_{}", bit_id);
				hop.src_location = yosys_netlist_reader::clean_src_location(c.src_raw);

				for (int out_bit : c.all_output_bits) {
					if (out_bit <= 0) {
						continue;
					}
					auto next_down = compute_downstream(out_bit);
					if (next_down.reaches_clock) {
						res.reaches_clock = true;
					}

					if (next_down.worst_reg.depth >= 0) {
						int new_depth = next_down.worst_reg.depth + 1;
						if (new_depth > res.worst_reg.depth) {
							res.worst_reg.depth = new_depth;
							res.worst_reg.endpoint_name = next_down.worst_reg.endpoint_name;
							res.worst_reg.end_kind = next_down.worst_reg.end_kind;
							res.worst_reg.end_src = next_down.worst_reg.end_src;
							res.worst_reg.hops = next_down.worst_reg.hops;
							res.worst_reg.hops.insert(res.worst_reg.hops.begin(), hop);
						}
					}

					if (next_down.worst_out.depth >= 0) {
						int new_depth = next_down.worst_out.depth + 1;
						if (new_depth > res.worst_out.depth) {
							res.worst_out.depth = new_depth;
							res.worst_out.endpoint_name = next_down.worst_out.endpoint_name;
							res.worst_out.end_kind = next_down.worst_out.end_kind;
							res.worst_out.end_src = next_down.worst_out.end_src;
							res.worst_out.hops = next_down.worst_out.hops;
							res.worst_out.hops.insert(res.worst_out.hops.begin(), hop);
						}
					}
				}
			}
		}
	}

	recursion_stack_.erase(bit_id);
	memo_downstream_[bit_id] = res;
	return res;
}

netlist_graph_analyzer::upstream_cache_entry netlist_graph_analyzer::compute_upstream(int bit_id)
{
	if (bit_id <= 0) {
		return {};
	}

	auto it = memo_upstream_.find(bit_id);
	if (it != memo_upstream_.end() && it->second.computed) {
		return it->second;
	}

	if (upstream_recursion_stack_.contains(bit_id)) {
		upstream_cache_entry loop_res;
		loop_res.computed = true;
		return loop_res;
	}

	upstream_recursion_stack_.insert(bit_id);

	upstream_cache_entry res;
	res.computed = true;

	auto it_drv = bit_drivers_.find(bit_id);
	if (it_drv != bit_drivers_.end()) {
		const auto &drv = it_drv->second;
		if (drv.type == bit_driver::driver_type::primary_input) {
			res.worst_in.depth = 0;
			res.worst_in.startpoint_name = drv.name;
			res.worst_in.start_kind = endpoint_kind::input_port;
		} else if (drv.type == bit_driver::driver_type::cell_q) {
			res.worst_reg.depth = 0;
			res.worst_reg.startpoint_name = std::format("{}.{}", drv.name, drv.pin_name);
			res.worst_reg.start_kind = endpoint_kind::register_cell;
			res.worst_reg.start_src = drv.src;
		} else if (drv.type == bit_driver::driver_type::cell_comb) {
			if (drv.cell_idx < cells_.size()) {
				const auto &c = cells_[drv.cell_idx];
				path_hop hop;
				hop.cell_name = c.name;
				hop.cell_type = c.type;
				hop.net_name = bit_to_net_name_.contains(bit_id) ? bit_to_net_name_[bit_id] : std::format("wire_{}", bit_id);
				hop.src_location = yosys_netlist_reader::clean_src_location(c.src_raw);

				for (int in_bit : c.all_input_bits) {
					if (in_bit <= 0) {
						continue;
					}
					auto prev_up = compute_upstream(in_bit);
					if (prev_up.worst_reg.depth >= 0) {
						int new_depth = prev_up.worst_reg.depth + 1;
						if (new_depth > res.worst_reg.depth) {
							res.worst_reg.depth = new_depth;
							res.worst_reg.startpoint_name = prev_up.worst_reg.startpoint_name;
							res.worst_reg.start_kind = prev_up.worst_reg.start_kind;
							res.worst_reg.start_src = prev_up.worst_reg.start_src;
							res.worst_reg.hops = prev_up.worst_reg.hops;
							res.worst_reg.hops.push_back(hop);
						}
					}
					if (prev_up.worst_in.depth >= 0) {
						int new_depth = prev_up.worst_in.depth + 1;
						if (new_depth > res.worst_in.depth) {
							res.worst_in.depth = new_depth;
							res.worst_in.startpoint_name = prev_up.worst_in.startpoint_name;
							res.worst_in.start_kind = prev_up.worst_in.start_kind;
							res.worst_in.start_src = prev_up.worst_in.start_src;
							res.worst_in.hops = prev_up.worst_in.hops;
							res.worst_in.hops.push_back(hop);
						}
					}
				}
			}
		}
	}

	upstream_recursion_stack_.erase(bit_id);
	memo_upstream_[bit_id] = res;
	return res;
}

void netlist_graph_analyzer::analyze_paths()
{
	// 1. Analyze primary input ports
	for (auto &pin : input_ports_) {
		pin.layers = -1;
		pin.endpoint = endpoint_kind::unconnected;
		pin.endpoint_desc = "Unconnected";

		// Search across all bits of this input port
		int worst_reg_depth = -1;
		std::string reg_sink_name;
		std::string reg_sink_src;
		std::vector<path_hop> worst_reg_hops;

		int worst_out_depth = -1;
		std::string out_sink_name;
		std::vector<path_hop> worst_out_hops;

		bool connects_to_clock = false;

		for (const auto &[bit_id, drv] : bit_drivers_) {
			if (drv.type == bit_driver::driver_type::primary_input && drv.name == pin.name) {
				auto down = compute_downstream(bit_id);
				if (down.reaches_clock) {
					connects_to_clock = true;
				}
				if (down.worst_reg.depth > worst_reg_depth) {
					worst_reg_depth = down.worst_reg.depth;
					reg_sink_name = down.worst_reg.endpoint_name;
					reg_sink_src = down.worst_reg.end_src;
					worst_reg_hops = down.worst_reg.hops;
				}
				if (down.worst_out.depth > worst_out_depth) {
					worst_out_depth = down.worst_out.depth;
					out_sink_name = down.worst_out.endpoint_name;
					worst_out_hops = down.worst_out.hops;
				}
			}
		}

		if (worst_reg_depth >= 0) {
			pin.layers = worst_reg_depth;
			pin.endpoint = endpoint_kind::register_cell;
			pin.endpoint_desc = "Register";
			pin.bound_signal_or_cell = reg_sink_name;

			timing_path tp;
			tp.category = path_category::in_to_reg;
			tp.depth = worst_reg_depth;
			tp.startpoint = pin.name;
			tp.start_kind = endpoint_kind::input_port;
			tp.endpoint = reg_sink_name;
			tp.end_kind = endpoint_kind::register_cell;
			tp.end_src = reg_sink_src;
			tp.hops = worst_reg_hops;
			all_in_to_reg_paths_.push_back(std::move(tp));
		} else if (worst_out_depth >= 0) {
			pin.layers = worst_out_depth;
			pin.endpoint = endpoint_kind::output_port;
			pin.endpoint_desc = "Output";
			pin.bound_signal_or_cell = out_sink_name;
		} else if (connects_to_clock) {
			pin.layers = -1;
			pin.endpoint = endpoint_kind::clock;
			pin.endpoint_desc = "Clock";
		}

		// Always record input to output path if one exists
		if (worst_out_depth >= 0) {
			timing_path tp;
			tp.category = path_category::in_to_out;
			tp.depth = worst_out_depth;
			tp.startpoint = pin.name;
			tp.start_kind = endpoint_kind::input_port;
			tp.endpoint = out_sink_name;
			tp.end_kind = endpoint_kind::output_port;
			tp.hops = worst_out_hops;
			all_in_to_out_paths_.push_back(std::move(tp));
		}
	}

	// 2. Analyze register Q outputs (Reg -> Reg and Reg -> Out)
	for (size_t c_idx = 0; c_idx < cells_.size(); ++c_idx) {
		const auto &c = cells_[c_idx];
		if (!c.is_sequential) {
			continue;
		}

		for (const auto &[pname, con_bits] : c.output_conns) {
			if (!is_sequential_source_pin(c.type, pname)) {
				continue;
			}

			for (size_t pin_idx = 0; pin_idx < con_bits.size(); ++pin_idx) {
				int q_bit = con_bits[pin_idx];
				if (q_bit <= 0) {
					continue;
				}

				auto down = compute_downstream(q_bit);
				std::string q_pin_name = std::format("{}.{}[{}]", c.name, pname, pin_idx);
				std::string q_src = yosys_netlist_reader::clean_src_location(c.src_raw);

				if (down.worst_reg.depth >= 0) {
					timing_path tp;
					tp.category = path_category::reg_to_reg;
					tp.depth = down.worst_reg.depth;
					tp.startpoint = q_pin_name;
					tp.start_kind = endpoint_kind::register_cell;
					tp.start_src = q_src;
					tp.endpoint = down.worst_reg.endpoint_name;
					tp.end_kind = endpoint_kind::register_cell;
					tp.end_src = down.worst_reg.end_src;
					tp.hops = down.worst_reg.hops;

					if (tp.depth > max_reg_to_reg_depth_) {
						max_reg_to_reg_depth_ = tp.depth;
					}
					all_reg_to_reg_paths_.push_back(std::move(tp));
				}
			}
		}
	}

	// 3. Analyze primary output ports using upstream trace
	for (auto &pout : output_ports_) {
		pout.layers = -1;
		pout.endpoint = endpoint_kind::unconnected;
		pout.endpoint_desc = "Unconnected";

		int worst_depth_reg = -1;
		std::string reg_src_name;
		std::string reg_src_src;
		std::vector<path_hop> reg_hops;

		int worst_depth_in = -1;
		std::string in_src_name;
		std::string in_src_src;
		std::vector<path_hop> in_hops;

		for (int bit_id : pout.bit_ids) {
			if (bit_id <= 0) {
				continue;
			}
			auto up = compute_upstream(bit_id);
			if (up.worst_reg.depth > worst_depth_reg) {
				worst_depth_reg = up.worst_reg.depth;
				reg_src_name = up.worst_reg.startpoint_name;
				reg_src_src = up.worst_reg.start_src;
				reg_hops = up.worst_reg.hops;
			}
			if (up.worst_in.depth > worst_depth_in) {
				worst_depth_in = up.worst_in.depth;
				in_src_name = up.worst_in.startpoint_name;
				in_src_src = up.worst_in.start_src;
				in_hops = up.worst_in.hops;
			}
		}

		if (worst_depth_reg >= 0) {
			pout.layers = worst_depth_reg;
			pout.endpoint = endpoint_kind::register_cell;
			pout.endpoint_desc = "Register";
			pout.bound_signal_or_cell = reg_src_name;

			timing_path tp;
			tp.category = path_category::reg_to_out;
			tp.depth = worst_depth_reg;
			tp.startpoint = reg_src_name;
			tp.start_kind = endpoint_kind::register_cell;
			tp.start_src = reg_src_src;
			tp.endpoint = pout.name;
			tp.end_kind = endpoint_kind::output_port;
			tp.hops = reg_hops;
			all_reg_to_out_paths_.push_back(std::move(tp));
		} else if (worst_depth_in >= 0) {
			pout.layers = worst_depth_in;
			pout.endpoint = endpoint_kind::input_port;
			pout.endpoint_desc = "Input";
			pout.bound_signal_or_cell = in_src_name;

			timing_path tp;
			tp.category = path_category::in_to_out;
			tp.depth = worst_depth_in;
			tp.startpoint = in_src_name;
			tp.start_kind = endpoint_kind::input_port;
			tp.start_src = in_src_src;
			tp.endpoint = pout.name;
			tp.end_kind = endpoint_kind::output_port;
			tp.hops = in_hops;
			all_in_to_out_paths_.push_back(std::move(tp));
		} else {
			pout.layers = 0;
			pout.endpoint = endpoint_kind::constant;
			pout.endpoint_desc = "Constant";
		}
	}
}

std::vector<timing_path> netlist_graph_analyzer::get_worst_paths(std::optional<path_category> category, size_t top_k) const
{
	std::vector<timing_path> candidates;

	auto append_filtered = [&](const std::vector<timing_path> &paths) {
		for (const auto &p : paths) {
			if (!category.has_value() || p.category == category.value()) {
				candidates.push_back(p);
			}
		}
	};

	append_filtered(all_reg_to_reg_paths_);
	append_filtered(all_in_to_reg_paths_);
	append_filtered(all_reg_to_out_paths_);
	append_filtered(all_in_to_out_paths_);

	std::sort(candidates.begin(), candidates.end(), [](const timing_path &a, const timing_path &b) {
		return a.depth > b.depth;
	});

	if (candidates.size() > top_k) {
		candidates.resize(top_k);
	}
	return candidates;
}

std::string netlist_graph_analyzer::generate_timing_markdown(std::string_view target_mode) const
{
	std::string md;
	md += std::format("# Timing: `{}`\n\n", module_name_);
	md += std::format("Target: {}\n", target_mode);
	md += std::format("Max Internal Reg -> Reg Depth: {}\n\n", max_reg_to_reg_depth_);

	md += "## Inputs\n\n";
	md += "| Name | Width | Layers to | Sinks to |\n";
	md += "| :--- | :---: | :---: | :--- |\n";
	for (const auto &inp : input_ports_) {
		std::string layers_str = (inp.layers < 0) ? "-" : std::to_string(inp.layers);
		md += std::format("| `{}` | {} | {} | {} |\n", inp.name, inp.bit_width, layers_str, inp.endpoint_desc);
	}
	md += "\n";

	md += "## Outputs\n\n";
	md += "| Name | Width | Layers from | Driven from |\n";
	md += "| :--- | :---: | :---: | :--- |\n";
	for (const auto &outp : output_ports_) {
		std::string layers_str = (outp.layers < 0) ? "-" : std::to_string(outp.layers);
		md += std::format("| `{}` | {} | {} | {} |\n", outp.name, outp.bit_width, layers_str, outp.endpoint_desc);
	}

	return md;
}

bool netlist_graph_analyzer::is_synthesis_source_file(const std::filesystem::path &p)
{
	std::string fname = p.filename().string();
	if (fname.ends_with('~') || fname.ends_with(".bak") || fname.ends_with(".org") ||
	    fname.ends_with(".twoport") || fname.ends_with(".orig") || fname.starts_with(".#") ||
	    fname.starts_with('.')) {
		return false;
	}
	if (fname.starts_with("tb_") || fname.ends_with("_tb.sv") || fname.ends_with("_tb.v") ||
	    fname.find("testbench") != std::string::npos) {
		return false;
	}
	for (const auto &part : p) {
		std::string s = part.string();
		if (s == ".git" || s == ".antigravitycli" || s == ".turbostar" || s == "build" ||
		    s == "obj_dir" || s == "tb" || s == "testbench" || s == "templates") {
			return false;
		}
	}
	auto ext = p.extension().string();
	return (ext == ".sv" || ext == ".v");
}

bool netlist_graph_analyzer::synthesize_and_load(std::string_view module_name, std::span<const std::string> source_files,
                                                 std::string_view target_mode, std::string &out_error)
{
	std::string yosys_bin = fs_utils::find_executable("yosys");
	if (yosys_bin.empty()) {
		out_error = "yosys executable not found in PATH";
		return false;
	}

	std::vector<std::string> files_to_use;
	if (!source_files.empty()) {
		for (const auto &f : source_files) {
			files_to_use.push_back(f);
		}
	} else {
		// Discover synthesizable .sv and .v files in project dir or current dir
		std::string proj_dir = fs_utils::get_project_dir();
		if (proj_dir.empty()) {
			proj_dir = ".";
		}
		try {
			for (const auto &entry : std::filesystem::recursive_directory_iterator(proj_dir)) {
				if (entry.is_regular_file() && is_synthesis_source_file(entry.path())) {
					files_to_use.push_back(entry.path().string());
				}
			}
		} catch (const std::exception &e) {
			out_error = std::format("Error scanning for Verilog files: {}", e.what());
			return false;
		}
	}

	if (files_to_use.empty()) {
		out_error = "No Verilog/SystemVerilog source files found to synthesize";
		return false;
	}

	std::string file_args;
	for (const auto &f : files_to_use) {
		file_args += " " + f;
	}

	std::string proj_root = fs_utils::get_project_dir();
	if (proj_root.empty()) {
		proj_root = ".";
	}
	std::error_code abs_ec;
	std::string abs_proj = std::filesystem::absolute(proj_root, abs_ec).string();
	std::string tmp_json = std::format("{}/.turbostar_yosys_{}_{}.json", abs_proj,
	                                   module_name.empty() ? "top" : module_name, (long)getpid());

	std::string synth_pipeline;
	if (target_mode == "iCE40") {
		if (module_name.empty()) {
			synth_pipeline = std::format("synth_ice40 -json {}", tmp_json);
		} else {
			synth_pipeline = std::format("synth_ice40 -top {} -json {}", module_name, tmp_json);
		}
	} else if (target_mode == "Generic LUT6") {
		std::string top_arg = module_name.empty() ? "-auto-top" : std::format("-top {}", module_name);
		synth_pipeline = std::format("hierarchy -check {}; proc; opt; flatten; opt; fsm; opt; memory; opt; techmap; abc -lut 6; write_json {}",
		                             top_arg, tmp_json);
	} else if (target_mode == "Generic Gates") {
		std::string top_arg = module_name.empty() ? "-auto-top" : std::format("-top {}", module_name);
		synth_pipeline = std::format("hierarchy -check {}; proc; opt; flatten; opt; fsm; opt; memory; opt; techmap; write_json {}",
		                             top_arg, tmp_json);
	} else {
		// Default: Generic LUT4
		std::string top_arg = module_name.empty() ? "-auto-top" : std::format("-top {}", module_name);
		synth_pipeline = std::format("hierarchy -check {}; proc; opt; flatten; opt; fsm; opt; memory; opt; techmap; abc -lut 4; write_json {}",
		                             top_arg, tmp_json);
	}

	std::string script = std::format("read_verilog -sv{}; {}", file_args, synth_pipeline);
	std::string cmd = std::format("{} -q -p {} 2>&1", fs_utils::escape_shell_arg(yosys_bin), fs_utils::escape_shell_arg(script));

	std::string yosys_out = fs_utils::execute_command_sync(cmd, 60);

	if (!std::filesystem::exists(tmp_json)) {
		out_error = std::format("Yosys synthesis failed to produce netlist JSON. Output:\n{}", yosys_out);
		return false;
	}

	bool load_ok = load_from_file(tmp_json, out_error, module_name);
	std::error_code ec;
	std::filesystem::remove(tmp_json, ec);
	return load_ok;
}

} // namespace verilog
