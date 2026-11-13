#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/yosys_netlist_reader.h"
#include "fs_utils.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <regex>
#include <map>
#include <set>
namespace verilog {

namespace fs = std::filesystem;

bool nextpnr_timing_analyzer::load_from_files(std::string_view timing_json_path, std::string_view netlist_json_path, std::string &out_error)
{
	netlist_dir_.clear();
	std::ifstream t_file{std::string(timing_json_path)};
	if (!t_file.is_open()) {
		out_error = std::format("Failed to open timing report '{}'", timing_json_path);
		return false;
	}

	nlohmann::json t_json;
	try {
		t_file >> t_json;
	} catch (const std::exception &e) {
		out_error = std::format("Error parsing timing JSON in '{}': {}", timing_json_path, e.what());
		return false;
	}

	if (!netlist_json_path.empty()) {
		fs::path nd = fs::path(netlist_json_path).parent_path();
		if ((nd.filename() == "synth" || nd.filename() == "build") && nd.has_parent_path()) {
			nd = nd.parent_path();
		}
		netlist_dir_ = nd.string();
		std::ifstream n_file{std::string(netlist_json_path)};
		if (n_file.is_open()) {
			try {
				nlohmann::json n_json;
				n_file >> n_json;
				parse_netlist_json(n_json);
			} catch (...) {
			}
		}
	} else {
		fs::path td = fs::path(timing_json_path).parent_path();
		if ((td.filename() == "synth" || td.filename() == "build") && td.has_parent_path()) {
			td = td.parent_path();
		}
		netlist_dir_ = td.string();
	}

	parse_timing_json(t_json);
	correlate_paths();
	return true;
}

bool nextpnr_timing_analyzer::load_from_json_strings(std::string_view timing_json_content, std::string_view netlist_json_content, std::string &out_error)
{
	netlist_dir_.clear();
	nlohmann::json t_json;
	try {
		t_json = nlohmann::json::parse(timing_json_content);
	} catch (const std::exception &e) {
		out_error = std::format("Error parsing timing JSON string: {}", e.what());
		return false;
	}

	if (!netlist_json_content.empty()) {
		try {
			nlohmann::json n_json = nlohmann::json::parse(netlist_json_content);
			parse_netlist_json(n_json);
		} catch (...) {
		}
	}

	parse_timing_json(t_json);
	correlate_paths();
	return true;
}

void nextpnr_timing_analyzer::parse_netlist_json(const nlohmann::json &j)
{
	netlist_cells_.clear();
	netlist_nets_.clear();
	net_alias_map_.clear();
	lut_to_packed_dff_.clear();
	dff_to_reg_name_.clear();
	cell_to_output_user_net_.clear();
	file_to_scopes_.clear();
	netlist_ports_.clear();
	bit_sink_counts_.clear();
	net_fanouts_.clear();
	top_module_name_.clear();

	if (!j.contains("modules") || !j["modules"].is_object()) {
		return;
	}

	const nlohmann::json *mod_obj = nullptr;
	for (const auto &[name, m] : j["modules"].items()) {
		if (m.contains("attributes") && m["attributes"].is_object()) {
			std::string top_attr = m["attributes"].value("top", "");
			if (top_attr == "1" || top_attr == "00000000000000000000000000000001") {
				mod_obj = &m;
				top_module_name_ = name;
				break;
			}
		}
	}

	if (!mod_obj && !j["modules"].empty()) {
		top_module_name_ = j["modules"].begin().key();
		mod_obj = &j["modules"].begin().value();
	}

	if (!mod_obj) return;

	if (mod_obj->contains("ports") && (*mod_obj)["ports"].is_object()) {
		for (const auto &[pname, pdata] : (*mod_obj)["ports"].items()) {
			netlist_ports_[pname] = pdata.value("direction", "inout");
		}
	}

	if (mod_obj->contains("cells") && (*mod_obj)["cells"].is_object()) {
		for (const auto &[cname, cdata] : (*mod_obj)["cells"].items()) {
			cell_info ci;
			ci.type = cdata.value("type", "");
			if (cdata.contains("attributes") && cdata["attributes"].is_object()) {
				ci.src_raw = cdata["attributes"].value("src", "");
			}
			netlist_cells_[cname] = std::move(ci);

			// Track input port connections to compute net sink fanout
			std::map<std::string, std::string> pdirs;
			if (cdata.contains("port_directions") && cdata["port_directions"].is_object()) {
				for (const auto &[pname, pdir] : cdata["port_directions"].items()) {
					if (pdir.is_string()) {
						pdirs[pname] = pdir.get<std::string>();
					}
				}
			}

			if (cdata.contains("connections") && cdata["connections"].is_object()) {
				for (const auto &[pname, conn_val] : cdata["connections"].items()) {
					bool is_input = false;
					auto it_dir = pdirs.find(pname);
					if (it_dir != pdirs.end()) {
						is_input = (it_dir->second == "input");
					} else {
						is_input = (pname != "Y" && pname != "Q" && pname != "O" && pname != "CO" && pname != "COUT");
					}

					if (is_input && conn_val.is_array()) {
						for (const auto &b_val : conn_val) {
							if (b_val.is_number_integer()) {
								int b = b_val.get<int>();
								if (b >= 0) {
									bit_sink_counts_[b]++;
								}
							}
						}
					}
				}
			}
		}
	}

	if (mod_obj->contains("netnames") && (*mod_obj)["netnames"].is_object()) {
		for (const auto &[nname, ndata] : (*mod_obj)["netnames"].items()) {
			net_info ni;
			if (ndata.contains("attributes") && ndata["attributes"].is_object()) {
				ni.src_raw = ndata["attributes"].value("src", "");
			}
			if (ndata.contains("bits") && ndata["bits"].is_array()) {
				size_t total_fanout = 0;
				for (const auto &b_val : ndata["bits"]) {
					if (b_val.is_number_integer()) {
						int b = b_val.get<int>();
						ni.bits.push_back(b);
						auto it = bit_sink_counts_.find(b);
						if (it != bit_sink_counts_.end()) {
							total_fanout += it->second;
						}
					}
				}
				net_fanouts_[nname] = total_fanout;
			}
			netlist_nets_[nname] = std::move(ni);
		}
	}

	auto corr = yosys_netlist_reader::build_correlation_tables(*mod_obj, netlist_dir_);
	net_alias_map_ = std::move(corr.net_alias_map);
	lut_to_packed_dff_ = std::move(corr.lut_to_packed_dff);
	dff_to_reg_name_ = std::move(corr.dff_to_reg_name);
	cell_to_output_user_net_ = std::move(corr.cell_to_output_user_net);
	file_to_scopes_ = std::move(corr.file_to_scopes);
}

void nextpnr_timing_analyzer::parse_timing_json(const nlohmann::json &j)
{
	clock_domains_.clear();
	utilization_.clear();
	critical_paths_.clear();
	detailed_endpoints_.clear();

	// Parse fmax
	if (j.contains("fmax") && j["fmax"].is_object()) {
		for (const auto &[domain, f_data] : j["fmax"].items()) {
			clock_domain_timing cdt;
			cdt.domain_name = domain;
			if (f_data.is_object()) {
				cdt.achieved_mhz = f_data.value("achieved", 0.0);
				cdt.constraint_mhz = f_data.value("constraint", 0.0);
				if (cdt.constraint_mhz > 0.0 && cdt.achieved_mhz > 0.0) {
					double period_target_ns = 1000.0 / cdt.constraint_mhz;
					double period_achieved_ns = 1000.0 / cdt.achieved_mhz;
					cdt.slack_ns = period_target_ns - period_achieved_ns;
					cdt.met = (cdt.slack_ns >= 0.0);
				}
			}
			clock_domains_.push_back(std::move(cdt));
		}
	}

	// Parse utilization
	if (j.contains("utilization") && j["utilization"].is_object()) {
		for (const auto &[res_name, u_data] : j["utilization"].items()) {
			if (u_data.is_object()) {
				device_resource_utilization dru;
				dru.resource_name = res_name;
				dru.used = u_data.value("used", 0);
				dru.available = u_data.value("available", 0);
				if (dru.available > 0) {
					dru.percentage = (static_cast<double>(dru.used) / static_cast<double>(dru.available)) * 100.0;
				}
				utilization_.push_back(std::move(dru));
			}
		}
	}

	// Parse critical_paths
	if (j.contains("critical_paths") && j["critical_paths"].is_array()) {
		for (const auto &p_obj : j["critical_paths"]) {
			if (!p_obj.is_object()) continue;

			timing_critical_path cp;
			std::string from_str = p_obj.value("from", "");
			std::string to_str = p_obj.value("to", "");

			bool from_clk = from_str.starts_with("posedge ") || from_str.starts_with("negedge ");
			bool to_clk = to_str.starts_with("posedge ") || to_str.starts_with("negedge ");

			if (from_clk && to_clk) {
				cp.path_type = timing_path_type::reg_to_reg;
			} else if (!from_clk && to_clk) {
				cp.path_type = timing_path_type::in_to_reg;
			} else if (from_clk && !to_clk) {
				cp.path_type = timing_path_type::reg_to_out;
			} else {
				cp.path_type = timing_path_type::in_to_out;
			}

			if (p_obj.contains("path") && p_obj["path"].is_array()) {
				for (const auto &hop_obj : p_obj["path"]) {
					if (!hop_obj.is_object()) continue;

					timing_hop h;
					h.type = hop_obj.value("type", "");
					h.delay_ns = hop_obj.value("delay", 0.0);
					h.net_name = hop_obj.value("net", "");

					int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
					bool has_from_loc = false;
					bool has_to_loc = false;

					if (hop_obj.contains("from") && hop_obj["from"].is_object()) {
						const auto &f_obj = hop_obj["from"];
						h.from_cell = f_obj.value("cell", "");
						h.from_port = f_obj.value("port", "");
						if (f_obj.contains("loc") && f_obj["loc"].is_array() && f_obj["loc"].size() == 2 &&
						    f_obj["loc"][0].is_number_integer() && f_obj["loc"][1].is_number_integer()) {
							x1 = f_obj["loc"][0].get<int>();
							y1 = f_obj["loc"][1].get<int>();
							h.from_loc = std::format("({}, {})", x1, y1);
							has_from_loc = true;
						}
					}
					if (hop_obj.contains("to") && hop_obj["to"].is_object()) {
						const auto &t_obj = hop_obj["to"];
						h.to_cell = t_obj.value("cell", "");
						h.to_port = t_obj.value("port", "");
						if (t_obj.contains("loc") && t_obj["loc"].is_array() && t_obj["loc"].size() == 2 &&
						    t_obj["loc"][0].is_number_integer() && t_obj["loc"][1].is_number_integer()) {
							x2 = t_obj["loc"][0].get<int>();
							y2 = t_obj["loc"][1].get<int>();
							h.to_loc = std::format("({}, {})", x2, y2);
							has_to_loc = true;
						}
					}

					if (has_from_loc && has_to_loc) {
						h.distance = std::abs(x1 - x2) + std::abs(y1 - y2);
					}
					cp.total_delay_ns += h.delay_ns;
					if (h.type == "logic") {
						cp.logic_delay_ns += h.delay_ns;
					} else if (h.type == "routing") {
						cp.routing_delay_ns += h.delay_ns;
					} else if (h.type == "clk-to-q") {
						cp.clk_to_q_delay_ns += h.delay_ns;
						cp.logic_delay_ns += h.delay_ns;
					} else if (h.type == "setup") {
						cp.setup_delay_ns += h.delay_ns;
					}

					cp.hops.push_back(std::move(h));
				}
			}

			// Identify startpoint and endpoint
			if (!cp.hops.empty()) {
				// Startpoint: first hop 'to' cell if clk-to-q or routing, else 'from' cell
				if (cp.hops.front().type == "clk-to-q") {
					cp.startpoint = cp.hops.front().to_cell;
				} else {
					cp.startpoint = cp.hops.front().from_cell;
				}
				// Endpoint: last hop 'to' cell
				cp.endpoint = cp.hops.back().to_cell;
			}

			critical_paths_.push_back(std::move(cp));
		}
	}

	double default_setup_ns = 0.0;
	for (const auto &cp : critical_paths_) {
		if (cp.path_type == timing_path_type::reg_to_reg && cp.setup_delay_ns > 0.0) {
			default_setup_ns = cp.setup_delay_ns;
			break;
		}
	}

	// Parse detailed_net_timings for multi-endpoint Top-K visibility
	if (j.contains("detailed_net_timings") && j["detailed_net_timings"].is_array()) {
		for (const auto &item : j["detailed_net_timings"]) {
			if (!item.is_object()) continue;
			std::string drv_cell = (item.contains("driver") && item["driver"].is_string()) ? item["driver"].get<std::string>() : "";
			std::string net_name = (item.contains("net") && item["net"].is_string()) ? item["net"].get<std::string>() : "";
			std::string net_event = (item.contains("event") && item["event"].is_string()) ? item["event"].get<std::string>() : "";
			if (!item.contains("endpoints") || !item["endpoints"].is_array()) continue;

			for (const auto &ep : item["endpoints"]) {
				if (!ep.is_object()) continue;
				std::string ep_cell = (ep.contains("cell") && ep["cell"].is_string()) ? ep["cell"].get<std::string>() : "";
				std::string ep_port = (ep.contains("port") && ep["port"].is_string()) ? ep["port"].get<std::string>() : "";
				std::string ep_event = (ep.contains("event") && ep["event"].is_string()) ? ep["event"].get<std::string>() : net_event;
				double arrival_ns = 0.0;
				if (ep.contains("delay")) {
					if (ep["delay"].is_number()) {
						arrival_ns = ep["delay"].get<double>();
					} else if (ep["delay"].is_array()) {
						for (const auto &d_val : ep["delay"]) {
							if (d_val.is_number()) {
								arrival_ns = std::max(arrival_ns, d_val.get<double>());
							}
						}
					}
				}
				if (ep_cell.empty() || arrival_ns <= 0.0) continue;

				bool to_clk = ep_event.starts_with("posedge ") || ep_event.starts_with("negedge ");
				timing_endpoint_summary tes;
				tes.raw_endpoint_cell = ep_cell;
				tes.raw_driver_cell = drv_cell;
				tes.raw_net_name = net_name;
				tes.port = ep_port;
				tes.path_type = to_clk ? timing_path_type::reg_to_reg : timing_path_type::reg_to_out;
				tes.total_delay_ns = arrival_ns + (to_clk ? default_setup_ns : 0.0);
				tes.implied_fmax_mhz = (tes.total_delay_ns > 0.0) ? (1000.0 / tes.total_delay_ns) : 0.0;
				detailed_endpoints_.push_back(std::move(tes));
			}
		}
	}
}

std::string nextpnr_timing_analyzer::normalize_cell_name(std::string_view raw_cell)
{
	std::string s(raw_cell);
	for (const auto &sfx : {"_DFFLC", "_LC", "_RAM", "$sb_io"}) {
		if (s.ends_with(sfx)) {
			s = s.substr(0, s.size() - std::string_view(sfx).size());
			break;
		}
	}
	return s;
}



size_t nextpnr_timing_analyzer::get_net_fanout(std::string_view net_name) const
{
	if (net_name.empty()) return 0;

	// Exact match
	auto it = net_fanouts_.find(std::string(net_name));
	if (it != net_fanouts_.end()) {
		return it->second;
	}

	// Bit index lookup e.g. "my_signal[2]"
	size_t bracket = net_name.rfind('[');
	if (bracket != std::string_view::npos && net_name.ends_with(']')) {
		std::string base(net_name.substr(0, bracket));
		std::string idx_str(net_name.substr(bracket + 1, net_name.size() - bracket - 2));
		try {
			size_t idx = std::stoul(idx_str);
			auto it_net = netlist_nets_.find(base);
			if (it_net != netlist_nets_.end() && idx < it_net->second.bits.size()) {
				int bit_id = it_net->second.bits[idx];
				auto it_bit = bit_sink_counts_.find(bit_id);
				if (it_bit != bit_sink_counts_.end()) {
					return it_bit->second;
				}
			}
		} catch (...) {
		}
	}

	// Normalized / base name lookup
	std::string norm = normalize_net_name(net_name);
	if (norm != net_name) {
		auto it_norm = net_fanouts_.find(norm);
		if (it_norm != net_fanouts_.end()) {
			return it_norm->second;
		}
		size_t norm_bracket = norm.rfind('[');
		if (norm_bracket != std::string::npos && norm.ends_with(']')) {
			std::string norm_base = norm.substr(0, norm_bracket);
			std::string norm_idx_str = norm.substr(norm_bracket + 1, norm.size() - norm_bracket - 2);
			try {
				size_t idx = std::stoul(norm_idx_str);
				auto it_net = netlist_nets_.find(norm_base);
				if (it_net != netlist_nets_.end() && idx < it_net->second.bits.size()) {
					int bit_id = it_net->second.bits[idx];
					auto it_bit = bit_sink_counts_.find(bit_id);
					if (it_bit != bit_sink_counts_.end()) {
						return it_bit->second;
					}
				}
			} catch (...) {
			}
		}
	}

	// Prefix lookup against netlist_nets_
	size_t best_len = 0;
	size_t best_fanout = 0;
	for (const auto &[nname, fanout] : net_fanouts_) {
		if (net_name.starts_with(nname) && nname.size() > best_len) {
			best_len = nname.size();
			best_fanout = fanout;
		}
	}
	return best_fanout;
}

std::string nextpnr_timing_analyzer::resolve_alias(std::string_view net_name) const
{
	if (net_name.empty()) {
		return "";
	}
	std::string s(net_name);
	auto it = net_alias_map_.find(s);
	if (it != net_alias_map_.end()) {
		return it->second;
	}
	size_t bracket = s.rfind('[');
	if (bracket != std::string::npos && s.ends_with(']')) {
		std::string base = s.substr(0, bracket);
		auto it_base = net_alias_map_.find(base);
		if (it_base != net_alias_map_.end()) {
			return it_base->second + s.substr(bracket);
		}
	}
	return s;
}

std::string nextpnr_timing_analyzer::resolve_cell_type(std::string_view raw_cell) const
{
	if (raw_cell.empty()) {
		return "";
	}
	std::string cand = normalize_cell_name(raw_cell);
	std::string matched_cell;
	if (netlist_cells_.contains(cand)) {
		matched_cell = cand;
	} else {
		size_t last_underscore = cand.rfind('_');
		if (last_underscore != std::string::npos && last_underscore + 1 < cand.size()) {
			bool all_digits = true;
			for (size_t i = last_underscore + 1; i < cand.size(); ++i) {
				if (!std::isdigit(cand[i])) {
					all_digits = false;
					break;
				}
			}
			if (all_digits) {
				std::string sub = cand.substr(0, last_underscore);
				if (netlist_cells_.contains(sub)) {
					matched_cell = sub;
				}
			}
		}
	}

	if (!matched_cell.empty()) {
		if (raw_cell.ends_with("_DFFLC") && lut_to_packed_dff_.contains(matched_cell)) {
			const std::string &dff_cell = lut_to_packed_dff_.at(matched_cell);
			if (netlist_cells_.contains(dff_cell) && !netlist_cells_.at(dff_cell).type.empty()) {
				return netlist_cells_.at(dff_cell).type;
			}
		}
		const auto &ci = netlist_cells_.at(matched_cell);
		if (!ci.type.empty()) {
			return ci.type;
		}
	}

	if (raw_cell.ends_with("_DFFLC")) return "ICESTORM_LC (DFF)";
	if (raw_cell.ends_with("_LC")) return "ICESTORM_LC";
	if (raw_cell.ends_with("_RAM")) return "SB_RAM40_4K";
	if (raw_cell.ends_with("$sb_io")) return "SB_IO";
	if (raw_cell.find("$gbuf") != std::string_view::npos) return "SB_GB";
	return "";
}

std::string nextpnr_timing_analyzer::resolve_cell_src(std::string_view raw_cell, std::string &out_hint, bool is_endpoint) const
{
	std::string cand = normalize_cell_name(raw_cell);
	out_hint = cand;

	// Check top ports
	if (netlist_ports_.contains(cand)) {
		out_hint = std::format("top port `{}`", cand);
		return "";
	}

	// Bit indexed port e.g. LED[7]
	size_t bracket = cand.find('[');
	if (bracket != std::string::npos) {
		std::string base_port = cand.substr(0, bracket);
		if (netlist_ports_.contains(base_port)) {
			out_hint = std::format("top port `{}`", cand);
			return "";
		}
	}

	std::string matched_cell;
	if (netlist_cells_.contains(cand)) {
		matched_cell = cand;
	} else {
		// Try stripping trailing slice index like _2
		size_t last_underscore = cand.rfind('_');
		if (last_underscore != std::string::npos && last_underscore + 1 < cand.size()) {
			bool all_digits = true;
			for (size_t i = last_underscore + 1; i < cand.size(); ++i) {
				if (!std::isdigit(cand[i])) {
					all_digits = false;
					break;
				}
			}
			if (all_digits) {
				std::string sub = cand.substr(0, last_underscore);
				if (netlist_cells_.contains(sub)) {
					matched_cell = sub;
				}
			}
		}
	}

	if ((is_endpoint || raw_cell.ends_with("_DFFLC")) && !matched_cell.empty() && lut_to_packed_dff_.contains(matched_cell)) {
		matched_cell = lut_to_packed_dff_.at(matched_cell);
		cand = matched_cell;
	}

	std::string src;
	std::string c_file;
	if (!matched_cell.empty()) {
		const auto &ci = netlist_cells_.at(matched_cell);
		src = yosys_netlist_reader::clean_src_location(ci.src_raw);
		size_t colon = src.rfind(':');
		c_file = (colon != std::string::npos) ? src.substr(0, colon) : src;
		if (dff_to_reg_name_.contains(matched_cell)) {
			out_hint = resolve_alias(normalize_net_name(dff_to_reg_name_.at(matched_cell)));
			return src;
		}
		if (ci.type.find("DFF") != std::string::npos) {
			std::string inferred = yosys_netlist_reader::infer_register_from_src(ci.src_raw, cand, netlist_dir_);
			if (!inferred.empty()) {
				out_hint = resolve_alias(normalize_net_name(inferred));
				return src;
			}
		}
		if (cell_to_output_user_net_.contains(matched_cell)) {
			out_hint = resolve_alias(normalize_net_name(cell_to_output_user_net_.at(matched_cell)));
			if (src.empty() || yosys_netlist_reader::is_library_source(src)) {
				std::string net_s = resolve_net_src(out_hint);
				if (!net_s.empty() && !yosys_netlist_reader::is_library_source(net_s)) {
					src = net_s;
				}
			}
			return src;
		}
	}

	// Netname prefix lookup (prefer clean user nets from the same RTL file, skip synthetic LUT/techmap intermediates)
	bool valid_c_file = !c_file.empty() && !yosys_netlist_reader::is_library_source(c_file);
	size_t best_same_file_len = 0;
	std::string best_same_file_net;
	std::string same_file_net_src;

	size_t best_len = 0;
	std::string best_net;
	std::string net_src;
	for (const auto &[nname, ndata] : netlist_nets_) {
		std::string cleaned_n_src = yosys_netlist_reader::clean_src_location(ndata.src_raw);
		if (cleaned_n_src.empty() || yosys_netlist_reader::is_library_source(cleaned_n_src)) continue;
		if (nname.find("_SB_") != std::string::npos || nname.find("$SB_") != std::string::npos ||
		    nname.find("$sb_") != std::string::npos || nname.find("$func$") != std::string::npos) {
			continue;
		}

		if (cand.starts_with(nname) && (cand.size() == nname.size() || cand[nname.size()] == '_' || cand[nname.size()] == '.')) {
			size_t n_colon = cleaned_n_src.rfind(':');
			std::string n_file = (n_colon != std::string::npos) ? cleaned_n_src.substr(0, n_colon) : cleaned_n_src;
			if (valid_c_file && n_file == c_file && nname.size() > best_same_file_len) {
				best_same_file_len = nname.size();
				best_same_file_net = nname;
				same_file_net_src = cleaned_n_src;
			}
			if (nname.size() > best_len) {
				best_len = nname.size();
				best_net = nname;
				net_src = cleaned_n_src;
			}
		}
	}

	if (!best_same_file_net.empty()) {
		best_net = best_same_file_net;
		net_src = same_file_net_src;
	}

	if (!best_net.empty()) {
		out_hint = resolve_alias(normalize_net_name(best_net));
		if (src.empty() || (yosys_netlist_reader::is_library_source(src) && !yosys_netlist_reader::is_library_source(net_src))) {
			src = net_src;
		}
	} else if (!matched_cell.empty()) {
		out_hint = resolve_alias(normalize_net_name(matched_cell));
	}

	return src;
}

std::string nextpnr_timing_analyzer::resolve_net_src(std::string_view raw_net) const
{
	if (raw_net.empty()) return "";

	std::string cand(raw_net);
	// Strip [bit_index]
	size_t bracket = cand.rfind('[');
	std::string base_net = (bracket != std::string::npos) ? cand.substr(0, bracket) : cand;

	// Strip $SB_IO_...
	size_t sb = base_net.find("$SB_IO_");
	std::string clean_net = (sb != std::string::npos) ? base_net.substr(0, sb) : base_net;

	for (const auto &n : {cand, base_net, clean_net}) {
		auto it = netlist_nets_.find(n);
		if (it != netlist_nets_.end()) {
			std::string s = yosys_netlist_reader::clean_src_location(it->second.src_raw);
			if (!s.empty()) return s;
		}
	}
	return "";
}

void nextpnr_timing_analyzer::correlate_paths()
{
	for (auto &cp : critical_paths_) {
		std::string hint;
		bool start_is_seq = (!cp.hops.empty() && cp.hops.front().type == "clk-to-q");
		cp.start_src = resolve_cell_src(cp.startpoint, hint, /*is_endpoint=*/start_is_seq);
		if (!hint.empty() && hint != normalize_cell_name(cp.startpoint)) {
			cp.startpoint = hint;
		}
		cp.startpoint = resolve_alias(normalize_net_name(cp.startpoint));

		cp.end_src = resolve_cell_src(cp.endpoint, hint, /*is_endpoint=*/true);
		if (!hint.empty() && hint != normalize_cell_name(cp.endpoint)) {
			cp.endpoint = hint;
		}
		cp.endpoint = resolve_alias(normalize_net_name(cp.endpoint));

		for (auto &h : cp.hops) {
			std::string target_cell = h.to_cell.empty() ? h.from_cell : h.to_cell;
			bool hop_is_seq = (h.type == "clk-to-q" || h.type == "setup");
			h.cell_src_location = resolve_cell_src(target_cell, h.cell_hint, /*is_endpoint=*/hop_is_seq);
			h.cell_type = resolve_cell_type(target_cell);
			h.cell_hint = resolve_alias(normalize_net_name(h.cell_hint));
			h.net_src_location = resolve_net_src(h.net_name);
			if ((h.cell_src_location.empty() || yosys_netlist_reader::is_library_source(h.cell_src_location)) &&
			    !h.net_src_location.empty() && !yosys_netlist_reader::is_library_source(h.net_src_location)) {
				h.cell_src_location = h.net_src_location;
			}
			if (h.type == "routing" && !h.net_name.empty()) {
				h.clean_net = resolve_alias(normalize_net_name(h.net_name));
				h.fanout = get_net_fanout(h.net_name);
			}
		}
	}

	for (auto &tes : detailed_endpoints_) {
		std::string hint;
		tes.end_src = resolve_cell_src(tes.raw_endpoint_cell, hint, /*is_endpoint=*/true);
		if (!hint.empty() && hint != normalize_cell_name(tes.raw_endpoint_cell)) {
			tes.endpoint = hint;
		} else {
			tes.endpoint = tes.raw_endpoint_cell;
		}
		tes.endpoint = resolve_alias(normalize_net_name(tes.endpoint));

		std::string drv_hint;
		tes.driver_src = resolve_cell_src(tes.raw_driver_cell, drv_hint, /*is_endpoint=*/false);
		std::string n_src = resolve_net_src(tes.raw_net_name);
		if ((tes.driver_src.empty() || yosys_netlist_reader::is_library_source(tes.driver_src)) &&
		    !n_src.empty() && !yosys_netlist_reader::is_library_source(n_src)) {
			tes.driver_src = n_src;
		}
		tes.clean_net = resolve_alias(normalize_net_name(tes.raw_net_name));
		tes.fanout = get_net_fanout(tes.raw_net_name);
	}
}

std::vector<timing_critical_path> nextpnr_timing_analyzer::get_filtered_paths(bool reg_to_reg_only, size_t top_k) const
{
	std::vector<timing_critical_path> res;
	for (const auto &p : critical_paths_) {
		if (reg_to_reg_only && p.path_type != timing_path_type::reg_to_reg) {
			continue;
		}
		res.push_back(p);
	}

	std::sort(res.begin(), res.end(), [](const timing_critical_path &a, const timing_critical_path &b) {
		return a.total_delay_ns > b.total_delay_ns;
	});

	if (res.size() > top_k) {
		res.resize(top_k);
	}
	return res;
}

std::vector<timing_bottleneck_group> nextpnr_timing_analyzer::get_sink_grouped_bottlenecks(bool reg_to_reg_only, size_t top_k) const
{
	std::map<std::string, timing_bottleneck_group> groups;

	for (const auto &p : critical_paths_) {
		if (reg_to_reg_only && p.path_type != timing_path_type::reg_to_reg) {
			continue;
		}

		auto &grp = groups[p.endpoint];
		if (grp.endpoint.empty()) {
			grp.endpoint = p.endpoint;
			grp.end_src = p.end_src;
			grp.path_type = p.path_type;
			grp.worst_delay_ns = p.total_delay_ns;
			grp.worst_logic_delay_ns = p.logic_delay_ns;
			grp.worst_routing_delay_ns = p.routing_delay_ns;
			grp.representative_hops = p.hops;
		} else if (p.total_delay_ns > grp.worst_delay_ns) {
			grp.worst_delay_ns = p.total_delay_ns;
			grp.worst_logic_delay_ns = p.logic_delay_ns;
			grp.worst_routing_delay_ns = p.routing_delay_ns;
			grp.representative_hops = p.hops;
		}

		if (std::find(grp.sources.begin(), grp.sources.end(), p.startpoint) == grp.sources.end()) {
			grp.sources.push_back(p.startpoint);
		}
	}

	std::vector<timing_bottleneck_group> result;
	for (auto &[_, g] : groups) {
		result.push_back(std::move(g));
	}

	std::sort(result.begin(), result.end(), [](const timing_bottleneck_group &a, const timing_bottleneck_group &b) {
		return a.worst_delay_ns > b.worst_delay_ns;
	});

	if (result.size() > top_k) {
		result.resize(top_k);
	}
	return result;
}

std::vector<timing_endpoint_summary> nextpnr_timing_analyzer::get_top_endpoint_timings(bool reg_to_reg_only, bool group_by_sink, size_t top_k) const
{
	std::vector<timing_endpoint_summary> sorted_eps;
	for (const auto &ep : detailed_endpoints_) {
		if (reg_to_reg_only && ep.path_type != timing_path_type::reg_to_reg) {
			continue;
		}
		sorted_eps.push_back(ep);
	}

	std::sort(sorted_eps.begin(), sorted_eps.end(), [](const timing_endpoint_summary &a, const timing_endpoint_summary &b) {
		if (std::abs(a.total_delay_ns - b.total_delay_ns) > 1e-6) {
			return a.total_delay_ns > b.total_delay_ns;
		}
		return a.endpoint < b.endpoint;
	});

	auto strip_bit = [](std::string_view s) -> std::string {
		size_t b = s.rfind('[');
		if (b != std::string_view::npos && s.ends_with(']')) {
			return std::string(s.substr(0, b));
		}
		return std::string(s);
	};

	auto leaf_name = [](std::string_view s) -> std::string {
		size_t d = s.rfind('.');
		return (d != std::string_view::npos) ? std::string(s.substr(d + 1)) : std::string(s);
	};

	std::vector<timing_endpoint_summary> result;
	if (group_by_sink) {
		std::set<std::string> seen_endpoints;
		for (auto &ep : sorted_eps) {
			std::string base_ep = strip_bit(ep.endpoint);
			if (seen_endpoints.contains(base_ep)) {
				continue;
			}
			seen_endpoints.insert(base_ep);
			ep.endpoint = base_ep;

			// Check if an existing result entry represents a sibling submodule instance
			// (same RTL source declaration and same leaf register name and same driving net base)
			bool merged_sibling = false;
			if (!ep.end_src.empty() && !yosys_netlist_reader::is_library_source(ep.end_src)) {
				std::string ep_leaf = leaf_name(ep.endpoint);
				std::string ep_net_base = strip_bit(ep.clean_net);
				for (auto &existing : result) {
					if (existing.end_src == ep.end_src &&
					    leaf_name(existing.endpoint) == ep_leaf &&
					    strip_bit(existing.clean_net) == ep_net_base) {
						existing.sibling_endpoints.push_back(ep.endpoint);
						merged_sibling = true;
						break;
					}
				}
			}

			if (!merged_sibling) {
				result.push_back(std::move(ep));
				if (result.size() >= top_k) {
					break;
				}
			}
		}
	} else {
		std::set<std::string> seen_cells;
		for (auto &ep : sorted_eps) {
			if (seen_cells.contains(ep.raw_endpoint_cell)) {
				continue;
			}
			seen_cells.insert(ep.raw_endpoint_cell);
			result.push_back(std::move(ep));
			if (result.size() >= top_k) {
				break;
			}
		}
	}

	return result;
}

bool nextpnr_timing_analyzer::synthesize_and_route(const fpga_target_config &cfg, std::span<const std::string> sources, std::string &out_error)
{
	std::string tmp_dir = fs_utils::get_project_tmp_dir();
	fs::path synth_json = fs::path(tmp_dir) / "pnr_synth.json";
	fs::path asc_file = fs::path(tmp_dir) / "pnr_out.asc";

	std::vector<std::string> rtl_sources;
	for (const auto &s : sources) {
		rtl_sources.push_back(s);
	}

	if (rtl_sources.empty()) {
		out_error = "No RTL source files provided for synthesis and place-and-route";
		return false;
	}

	std::string top_mod = cfg.top_module.empty() ? "main" : cfg.top_module;

	// 1. Run Yosys with each source file passed as a separate argument
	std::string synth_pass;
	if (cfg.target == "ice40") {
		synth_pass = "synth_ice40";
		if (!cfg.yosys_synth_args.empty()) {
			synth_pass += " " + cfg.yosys_synth_args;
		} else {
			synth_pass += " -abc9";
		}
		synth_pass += " -top " + top_mod + " -json " + synth_json.string();
	} else if (cfg.target == "ecp5") {
		synth_pass = "synth_ecp5 -top " + top_mod + " -json " + synth_json.string();
	} else {
		// Generic
		synth_pass = "synth -top " + top_mod + "; write_json " + synth_json.string();
	}

	std::string yosys_cmd = "yosys -p " + fs_utils::escape_shell_arg(synth_pass);
	for (const auto &src : rtl_sources) {
		yosys_cmd += " " + fs_utils::escape_shell_arg(src);
	}

	std::string y_out = fs_utils::execute_command_sync(yosys_cmd);
	if (!fs::exists(synth_json)) {
		out_error = std::format("Yosys synthesis failed to generate netlist:\n{}", y_out);
		return false;
	}

	// 2. Run nextpnr (adaptive multi-seed execution when individual runs are fast)
	std::string pnr_tool = (cfg.target == "ice40") ? "nextpnr-ice40" :
	                       (cfg.target == "ecp5") ? "nextpnr-ecp5" : "nextpnr-generic";

	std::string pnr_args = "--json " + fs_utils::escape_shell_arg(synth_json.string()) +
	                       " --detailed-timing-report";

	if (cfg.target == "ice40") {
		pnr_args += " --asc " + fs_utils::escape_shell_arg(asc_file.string());
		pnr_args += " --placer heap";
		if (!cfg.device.empty()) {
			if (cfg.device.starts_with("-")) {
				pnr_args += " " + cfg.device;
			} else {
				pnr_args += " --" + cfg.device;
			}
		} else {
			pnr_args += " --hx8k";
		}

		if (!cfg.package.empty()) {
			if (cfg.package.starts_with("-")) {
				pnr_args += " " + cfg.package;
			} else {
				pnr_args += " --package " + cfg.package;
			}
		}

		if (!cfg.pcf_file.empty()) {
			pnr_args += " --pcf " + fs_utils::escape_shell_arg(cfg.pcf_file);
		}

		if (cfg.freq_mhz > 0.0) {
			pnr_args += std::format(" --freq {:.1f}", cfg.freq_mhz);
		}
	} else if (cfg.target == "generic") {
		pnr_args += " --uarch example";
		if (cfg.freq_mhz > 0.0) {
			pnr_args += std::format(" --freq {:.1f}", cfg.freq_mhz);
		}
	}

	struct seed_run {
		int seed{1};
		fs::path timing_path;
		double primary_fmax{0.0};
		std::map<std::string, double> domain_fmax;
	};
	std::vector<seed_run> seed_runs;

	auto parse_seed_fmax = [](int seed, const fs::path &t_path, seed_run &out_run) -> bool {
		std::ifstream ifs(t_path);
		if (!ifs.is_open()) return false;
		try {
			nlohmann::json j;
			ifs >> j;
			out_run.seed = seed;
			out_run.timing_path = t_path;
			if (j.contains("fmax") && j["fmax"].is_object()) {
				for (const auto &[dom, f_obj] : j["fmax"].items()) {
					if (f_obj.is_object() && f_obj.contains("achieved") && f_obj["achieved"].is_number()) {
						double f = f_obj["achieved"].get<double>();
						out_run.domain_fmax[dom] = f;
						if (out_run.primary_fmax <= 0.0) {
							out_run.primary_fmax = f;
						}
					}
				}
			}
			return true;
		} catch (...) {
			return false;
		}
	};

	fs::path seed1_timing = fs::path(tmp_dir) / "pnr_timing_seed1.json";
	std::error_code ec_rm;
	fs::remove(seed1_timing, ec_rm);

	auto t_start = std::chrono::steady_clock::now();
	std::string full_pnr_cmd = std::format("{} {} --seed 1 --report {}",
	                                       pnr_tool, pnr_args, fs_utils::escape_shell_arg(seed1_timing.string()));
	std::string p_out = fs_utils::execute_command_sync(full_pnr_cmd);
	auto t_end = std::chrono::steady_clock::now();
	double last_seed_sec = std::chrono::duration<double>(t_end - t_start).count();
	double total_pnr_sec = last_seed_sec;

	if (!fs::exists(seed1_timing)) {
		out_error = std::format("nextpnr place-and-route failed to generate timing report:\n{}", p_out);
		return false;
	}

	seed_run sr1;
	if (parse_seed_fmax(1, seed1_timing, sr1)) {
		seed_runs.push_back(std::move(sr1));
	}

	// Run additional seeds (up to 5 total) as long as we are well within a 10-second budget
	for (int seed = 2; seed <= 5; ++seed) {
		if (total_pnr_sec >= 10.0 || (total_pnr_sec + last_seed_sec) >= 10.0) {
			break;
		}
		fs::path seed_timing = fs::path(tmp_dir) / std::format("pnr_timing_seed{}.json", seed);
		fs::remove(seed_timing, ec_rm);

		auto s_start = std::chrono::steady_clock::now();
		std::string seed_cmd = std::format("{} {} --seed {} --report {}",
		                                   pnr_tool, pnr_args, seed, fs_utils::escape_shell_arg(seed_timing.string()));
		fs_utils::execute_command_sync(seed_cmd);
		auto s_end = std::chrono::steady_clock::now();
		last_seed_sec = std::chrono::duration<double>(s_end - s_start).count();
		total_pnr_sec += last_seed_sec;

		if (fs::exists(seed_timing)) {
			seed_run sr;
			if (parse_seed_fmax(seed, seed_timing, sr)) {
				seed_runs.push_back(std::move(sr));
			}
		}
	}

	auto update_rtl_base_dir = [&]() {
		if (!rtl_sources.empty()) {
			fs::path sdir = fs::path(rtl_sources.front()).parent_path();
			if ((sdir.filename() == "sv" || sdir.filename() == "src" || sdir.filename() == "rtl") && sdir.has_parent_path()) {
				sdir = sdir.parent_path();
			}
			netlist_dir_ = sdir.string();
		}
	};

	if (seed_runs.empty()) {
		if (!load_from_files(seed1_timing.string(), synth_json.string(), out_error)) {
			return false;
		}
		update_rtl_base_dir();
		return true;
	}

	std::sort(seed_runs.begin(), seed_runs.end(), [](const seed_run &a, const seed_run &b) {
		return a.primary_fmax < b.primary_fmax;
	});

	const auto &median_run = seed_runs[seed_runs.size() / 2];
	if (!load_from_files(median_run.timing_path.string(), synth_json.string(), out_error)) {
		return false;
	}
	update_rtl_base_dir();

	if (seed_runs.size() > 1) {
		for (auto &cd : clock_domains_) {
			double min_f = cd.achieved_mhz;
			double max_f = cd.achieved_mhz;
			for (const auto &sr : seed_runs) {
				auto it = sr.domain_fmax.find(cd.domain_name);
				if (it != sr.domain_fmax.end()) {
					min_f = std::min(min_f, it->second);
					max_f = std::max(max_f, it->second);
				}
			}
			cd.min_achieved_mhz = min_f;
			cd.max_achieved_mhz = max_f;
			cd.seed_count = seed_runs.size();
		}
	}

	return true;
}

std::string nextpnr_timing_analyzer::generate_timing_markdown(bool reg_to_reg_only, bool group_by_sink, size_t top_k, bool collapse, bool show_segments, int show_code) const
{
	std::string md;

	// 1. Clock domains summary
	if (!clock_domains_.empty()) {
		md += "## Clock Domain & Frequency Timing\n\n";
		md += "| Clock Domain | Constraint | Achieved Fmax | Slack | Status |\n";
		md += "| :--- | :---: | :---: | :---: | :---: |\n";
		for (const auto &cd : clock_domains_) {
			std::string status = cd.met ? "**MET**" : "**VIOLATED**";
			std::string slack_str = (cd.slack_ns >= 0.0) ? std::format("+{:.3f} ns", cd.slack_ns) : std::format("{:.3f} ns", cd.slack_ns);
			std::string fmax_str = (cd.seed_count > 1)
				? std::format("**{:.2f} MHz** (median of {} seeds: {:.2f}–{:.2f} MHz)",
				              cd.achieved_mhz, cd.seed_count, cd.min_achieved_mhz, cd.max_achieved_mhz)
				: std::format("**{:.2f} MHz**", cd.achieved_mhz);
			md += std::format("| `{}` | {:.2f} MHz | {} | {} | {} |\n",
			                  cd.domain_name, cd.constraint_mhz, fmax_str, slack_str, status);
		}
		md += "\n";
	}

	// 2. Hardware Resource Utilization
	if (!utilization_.empty()) {
		md += "## Hardware Resource Utilization\n\n";
		md += "| Resource | Used | Available | Utilization |\n";
		md += "| :--- | :---: | :---: | :---: |\n";
		for (const auto &u : utilization_) {
			if (u.used > 0 || u.available > 0) {
				md += std::format("| `{}` | {} | {} | {:.1f}% |\n",
				                  u.resource_name, u.used, u.available, u.percentage);
			}
		}
		md += "\n";
	}

	auto format_hops_chain = [collapse, show_code, this](std::span<const timing_hop> hops) -> std::string {
		struct stage {
			std::string raw_src;
			std::string display_loc;
			std::string base_hint;
			std::string first_cell;
			std::string last_cell;
			bool has_routing{false};
			double logic_delay{0.0};
			double routing_delay{0.0};
			size_t luts{0};
			size_t hops_count{0};
			std::string out_net;
			size_t max_fanout{0};
			int max_distance{0};
			std::set<int> bit_indices;
			std::string bit_var_name;
		};
		std::vector<stage> stages;

		auto get_base_signal = [](std::string_view s) -> std::string {
			if (s.empty()) return "";
			std::string norm = normalize_net_name(s);
			size_t br = norm.rfind('[');
			if (br != std::string::npos && norm.ends_with(']')) {
				return norm.substr(0, br);
			}
			return norm;
		};

		auto extract_bit_index = [](std::string_view s) -> std::optional<int> {
			size_t br = s.rfind('[');
			if (br != std::string::npos && s.ends_with(']')) {
				std::string_view num_str = s.substr(br + 1, s.size() - br - 2);
				int val = 0;
				auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
				if (ec == std::errc()) return val;
			}
			return std::nullopt;
		};

		auto get_leaf_name = [](std::string_view s) -> std::string {
			size_t dot = s.rfind('.');
			if (dot != std::string_view::npos && dot + 1 < s.size()) {
				return std::string(s.substr(dot + 1));
			}
			return std::string(s);
		};

		for (const auto &h : hops) {
			if (h.type == "source" || h.type == "setup") continue;

			std::string loc = h.cell_src_location;
			if ((loc.empty() || yosys_netlist_reader::is_library_source(loc)) &&
			    !h.net_src_location.empty() && !yosys_netlist_reader::is_library_source(h.net_src_location)) {
				loc = h.net_src_location;
			}

			std::string hop_cell = h.to_cell.empty() ? h.from_cell : h.to_cell;
			std::string from_cell = h.from_cell.empty() ? hop_cell : h.from_cell;
			std::string to_cell = h.to_cell.empty() ? hop_cell : h.to_cell;

			std::string norm_hint = normalize_net_name(h.cell_hint);
			std::string norm_net = normalize_net_name(h.clean_net.empty() ? h.net_name : h.clean_net);

			std::string cur_base_hint = get_base_signal(norm_hint);
			std::string cur_base_net = get_base_signal(norm_net);
			std::string effective_base = !cur_base_hint.empty() ? cur_base_hint : cur_base_net;

			bool can_merge = false;
			if (!stages.empty()) {
				auto &back = stages.back();
				if (!back.has_routing && (back.last_cell == from_cell || back.first_cell == from_cell)) {
					can_merge = true;
				} else if (collapse) {
					bool both_lib = yosys_netlist_reader::is_library_source(back.raw_src) &&
					                yosys_netlist_reader::is_library_source(loc);
					bool same_loc = !loc.empty() && !yosys_netlist_reader::is_library_source(loc) &&
					                back.raw_src == loc;
					bool same_base = !effective_base.empty() &&
					                 (back.base_hint == effective_base ||
					                  get_base_signal(back.out_net) == effective_base);

					if (both_lib && (same_base || back.display_loc == clean_display_location(loc))) {
						can_merge = true;
					} else if (same_loc) {
						can_merge = true;
					}
				}
			}

			if (can_merge) {
				auto &st = stages.back();
				st.hops_count++;
				if (st.base_hint.empty() && !effective_base.empty()) {
					st.base_hint = effective_base;
				}
				if (h.type == "logic" || h.type == "clk-to-q") {
					st.last_cell = to_cell;
					st.has_routing = false;
					st.logic_delay += h.delay_ns;
					st.luts++;
				} else if (h.type == "routing") {
					st.has_routing = true;
					st.routing_delay += h.delay_ns;
					if (!norm_net.empty()) {
						st.out_net = norm_net;
					}
					if ((st.base_hint.empty() || st.base_hint == normalize_cell_name(st.first_cell)) && !cur_base_net.empty()) {
						st.base_hint = cur_base_net;
					}
					st.max_fanout = std::max(st.max_fanout, h.fanout);
					st.max_distance = std::max(st.max_distance, h.distance);

					auto bit_idx = extract_bit_index(norm_net);
					if (bit_idx) {
						st.bit_indices.insert(*bit_idx);
						if (st.bit_var_name.empty()) {
							st.bit_var_name = get_leaf_name(cur_base_net);
						}
					}
				}
			} else {
				stage st;
				st.raw_src = loc;
				st.display_loc = clean_display_location(loc);
				st.base_hint = effective_base;
				st.first_cell = from_cell;
				st.last_cell = to_cell;
				st.hops_count = 1;

				if (h.type == "logic" || h.type == "clk-to-q") {
					st.logic_delay = h.delay_ns;
					st.luts = 1;
				} else if (h.type == "routing") {
					st.has_routing = true;
					st.routing_delay = h.delay_ns;
					st.out_net = norm_net;
					st.max_fanout = h.fanout;
					st.max_distance = h.distance;

					auto bit_idx = extract_bit_index(norm_net);
					if (bit_idx) {
						st.bit_indices.insert(*bit_idx);
						st.bit_var_name = get_leaf_name(cur_base_net);
					}
				}
				stages.push_back(std::move(st));
			}
		}

		std::string chain_md;
		for (size_t s_idx = 0; s_idx < stages.size(); ++s_idx) {
			const auto &st = stages[s_idx];
			std::string loc_str = st.display_loc.empty() ? "[interconnect]" : std::format("`{}`", st.display_loc);
			std::string hint_str;

			bool is_collapsed = collapse && st.luts > 1;
			if (is_collapsed) {
				if (st.base_hint.empty()) {
					hint_str = std::format(" ({} carry/LUT hops)", st.luts);
				} else {
					hint_str = std::format(" ({}, {} carry/LUT hops)", st.base_hint, st.luts);
				}
			} else if (!st.base_hint.empty()) {
				hint_str = std::format(" ({})", st.base_hint);
			}

			std::string delay_str;
			if (is_collapsed && !st.bit_indices.empty()) {
				int min_b = *st.bit_indices.begin();
				int max_b = *st.bit_indices.rbegin();
				std::string across_str = (st.bit_indices.size() > 1) ?
					std::format(" across {}[{}..{}]", st.bit_var_name, min_b, max_b) :
					std::format(" across {}[{}]", st.bit_var_name, min_b);

				delay_str = std::format("— **{:.3f} ns** ({:.3f} ns logic, {:.3f} ns routing{})",
				                        st.logic_delay + st.routing_delay, st.logic_delay, st.routing_delay, across_str);
			} else {
				delay_str = std::format("— **{:.3f} ns** ({:.3f} ns logic, {:.3f} ns routing)",
				                        st.logic_delay + st.routing_delay, st.logic_delay, st.routing_delay);
			}

			std::string out_str;
			if (is_collapsed) {
				if (st.max_fanout > 0 || st.max_distance > 0) {
					std::string meta;
					if (st.max_fanout > 0) {
						meta += std::format("max {} sink{}", st.max_fanout, st.max_fanout == 1 ? "" : "s");
					}
					if (st.max_distance > 0) {
						if (!meta.empty()) meta += ", ";
						meta += std::format("max span: {} tile{}", st.max_distance, st.max_distance == 1 ? "" : "s");
					}
					out_str = std::format(" [{}]", meta);
				}
			} else {
				if (!st.out_net.empty() && st.out_net.find('$') == std::string::npos && st.out_net != st.base_hint) {
					out_str = std::format(" -> `{}`", st.out_net);
				}
				if (st.max_fanout > 0 || st.max_distance > 0) {
					std::string meta;
					if (st.max_fanout > 0) {
						meta += std::format("{} sink{}", st.max_fanout, st.max_fanout == 1 ? "" : "s");
					}
					if (st.max_distance > 0) {
						if (!meta.empty()) meta += ", ";
						meta += std::format("span: {} tile{}", st.max_distance, st.max_distance == 1 ? "" : "s");
					}
					out_str += std::format(" [{}]", meta);
				}
			}

			std::string snippet_inline;
			std::string snippet_block;
			if (show_code > 0 && !st.raw_src.empty() && !yosys_netlist_reader::is_library_source(st.raw_src)) {
				std::string sig_hint = !st.base_hint.empty() ? st.base_hint : st.out_net;
				std::string snip = yosys_netlist_reader::extract_source_snippet(st.raw_src, sig_hint, netlist_dir_, show_code);
				if (!snip.empty()) {
					if (show_code == 1) {
						snippet_inline = std::format(" | `{}`", snip);
					} else {
						snippet_block = "    ```systemverilog\n";
						size_t pos = 0;
						while (pos < snip.size()) {
							size_t nl = snip.find('\n', pos);
							std::string_view line_v = (nl == std::string::npos)
								? std::string_view(snip).substr(pos)
								: std::string_view(snip).substr(pos, nl - pos);
							snippet_block += std::format("    {}\n", line_v);
							if (nl == std::string::npos) break;
							pos = nl + 1;
						}
						snippet_block += "    ```\n";
					}
				}
			}

			chain_md += std::format("  {}. {}{}{} {}{}\n{}",
			                        s_idx + 1, loc_str, hint_str, snippet_inline, delay_str, out_str, snippet_block);
		}
		return chain_md;
	};

	auto format_end_loc = [show_code, this](std::string_view raw_src, std::string_view ep_name) -> std::string {
		std::string disp = clean_display_location(raw_src);
		if (disp.empty()) return "";
		if (show_code == 1 && !yosys_netlist_reader::is_library_source(raw_src)) {
			std::string snip = yosys_netlist_reader::extract_source_snippet(raw_src, ep_name, netlist_dir_, 1);
			if (!snip.empty()) {
				return std::format("(`{}` | `{}`)", disp, snip);
			}
		}
		return std::format("(`{}`)", disp);
	};

	auto format_hops_segments = [show_code, this](std::span<const timing_hop> hops) -> std::string {
		if (hops.empty()) return "";
		std::string seg_md = "- **Segments (Hop-by-Hop)**:\n";
		size_t seg_idx = 0;
		for (const auto &h : hops) {
			if (h.type == "source") continue;
			seg_idx++;
			if (h.type == "routing") {
				std::string from_pin = h.from_port.empty() ? h.from_cell : std::format("{}.{}", h.from_cell, h.from_port);
				std::string to_pin = h.to_port.empty() ? h.to_cell : std::format("{}.{}", h.to_cell, h.to_port);
				std::string clean_n = h.clean_net.empty() ? normalize_net_name(h.net_name) : h.clean_net;
				std::string net_str;
				if (!clean_n.empty()) {
					if (!h.net_name.empty() && h.net_name != clean_n) {
						net_str = std::format("net `{}` (raw: `{}`)", clean_n, h.net_name);
					} else {
						net_str = std::format("net `{}`", clean_n);
					}
				} else if (!h.net_name.empty()) {
					net_str = std::format("net `{}`", h.net_name);
				} else {
					net_str = "interconnect";
				}
				std::string meta;
				if (h.fanout > 0) {
					meta += std::format("fanout: {}", h.fanout);
				}
				if (h.distance > 0) {
					if (!meta.empty()) meta += ", ";
					if (!h.from_loc.empty() && !h.to_loc.empty()) {
						meta += std::format("span: {} tile{} {} -> {}", h.distance, h.distance == 1 ? "" : "s", h.from_loc, h.to_loc);
					} else {
						meta += std::format("span: {} tile{}", h.distance, h.distance == 1 ? "" : "s");
					}
				}
				std::string n_loc = clean_display_location(h.net_src_location);
				if (!n_loc.empty() && !yosys_netlist_reader::is_library_source(h.net_src_location)) {
					if (!meta.empty()) meta += ", ";
					meta += std::format("`{}`", n_loc);
				}
				std::string meta_str = meta.empty() ? "" : std::format(" [{}]", meta);
				seg_md += std::format("  {}. `[routing]` **{:.3f} ns** — {} (`{}` -> `{}`){}\n",
				                      seg_idx, h.delay_ns, net_str, from_pin, to_pin, meta_str);
			} else {
				std::string cell_name = h.to_cell.empty() ? h.from_cell : h.to_cell;
				std::string ctype = h.cell_type.empty() ? "cell" : h.cell_type;
				std::string pin_str;
				if (!h.from_port.empty() && !h.to_port.empty() && h.from_port != h.to_port) {
					pin_str = std::format(" (`{}` -> `{}`)", h.from_port, h.to_port);
				} else if (!h.to_port.empty()) {
					pin_str = std::format(" (pin `{}`)", h.to_port);
				} else if (!h.from_port.empty()) {
					pin_str = std::format(" (pin `{}`)", h.from_port);
				}
				std::string hint_str;
				std::string norm_cell = normalize_cell_name(cell_name);
				if (!h.cell_hint.empty() && h.cell_hint != cell_name && h.cell_hint != norm_cell) {
					hint_str = std::format(" `{}` (cell `{}`)", h.cell_hint, cell_name);
				} else if (!cell_name.empty()) {
					hint_str = std::format(" `{}`", cell_name);
				}
				std::string loc_coord = !h.to_loc.empty() ? h.to_loc : h.from_loc;
				std::string coord_str = loc_coord.empty() ? "" : std::format(" @ {}", loc_coord);
				std::string rtl_loc = clean_display_location(h.cell_src_location);
				std::string rtl_str;
				std::string snippet_block;
				if (!rtl_loc.empty()) {
					std::string sig_hint = !h.cell_hint.empty() ? h.cell_hint : norm_cell;
					if (show_code > 0 && !yosys_netlist_reader::is_library_source(h.cell_src_location)) {
						std::string snip = yosys_netlist_reader::extract_source_snippet(h.cell_src_location, sig_hint, netlist_dir_, show_code);
						if (!snip.empty()) {
							if (show_code == 1) {
								rtl_str = std::format(" [`{}` | `{}`]", rtl_loc, snip);
							} else {
								rtl_str = std::format(" [`{}`]", rtl_loc);
								snippet_block = std::format("     ```systemverilog\n{}     ```\n", snip);
							}
						} else {
							rtl_str = std::format(" [`{}`]", rtl_loc);
						}
					} else {
						rtl_str = std::format(" [`{}`]", rtl_loc);
					}
				}
				seg_md += std::format("  {}. `[{}]` **{:.3f} ns** — `{}`{}{}{}{}\n{}",
				                      seg_idx, h.type, h.delay_ns, ctype, hint_str, pin_str, coord_str, rtl_str, snippet_block);
			}
		}
		return seg_md;
	};

	auto format_dominant_routing = [](std::span<const timing_hop> hops) -> std::string {
		std::vector<const timing_hop *> routing_hops;
		for (const auto &h : hops) {
			if (h.type == "routing" && h.delay_ns > 0.05 && !h.net_name.empty()) {
				routing_hops.push_back(&h);
			}
		}
		if (routing_hops.empty()) return "";

		std::sort(routing_hops.begin(), routing_hops.end(), [](const timing_hop *a, const timing_hop *b) {
			return a->delay_ns > b->delay_ns;
		});

		std::string res = "- **Dominant Routing Delays**:\n";
		size_t count = std::min<size_t>(3, routing_hops.size());
		for (size_t i = 0; i < count; ++i) {
			const auto *rh = routing_hops[i];
			std::string n = normalize_net_name(rh->clean_net.empty() ? rh->net_name : rh->clean_net);
			std::string meta;
			if (rh->fanout > 0) {
				meta += std::format("{} sink{}", rh->fanout, rh->fanout == 1 ? "" : "s");
			}
			if (rh->distance > 0) {
				if (!meta.empty()) meta += ", ";
				meta += std::format("span: {} tile{}", rh->distance, rh->distance == 1 ? "" : "s");
			}
			std::string meta_str = meta.empty() ? "" : std::format(" ({})", meta);
			res += std::format("  - **{:.3f} ns** routing via `{}`{}\n", rh->delay_ns, n, meta_str);
		}
		return res;
	};

	auto append_additional_endpoints = [&](size_t start_num, std::set<std::string> &printed_endpoints) {
		if (start_num >= top_k) return;
		auto extra_eps = get_top_endpoint_timings(reg_to_reg_only, group_by_sink, top_k + start_num);
		size_t path_num = start_num;
		for (const auto &ep : extra_eps) {
			if (path_num >= top_k) break;
			if (printed_endpoints.contains(ep.endpoint)) continue;
			printed_endpoints.insert(ep.endpoint);
			path_num++;

			std::string sib_str;
			if (!ep.sibling_endpoints.empty()) {
				sib_str = " (+ ";
				for (size_t s = 0; s < ep.sibling_endpoints.size(); ++s) {
					if (s > 0) sib_str += ", ";
					sib_str += std::format("`{}`", ep.sibling_endpoints[s]);
				}
				sib_str += ")";
			}

			md += std::format("### Path #{} [Reg -> Reg] Delay: **{:.3f} ns** (Implied Fmax: **{:.2f} MHz**) — Converging on `{}`{}\n",
			                  path_num, ep.total_delay_ns, ep.implied_fmax_mhz, ep.endpoint, sib_str);
			if (!ep.clean_net.empty()) {
				std::string drv_loc = clean_display_location(ep.driver_src);
				std::string meta;
				if (!drv_loc.empty()) {
					if (show_code == 1 && !yosys_netlist_reader::is_library_source(ep.driver_src)) {
						std::string snip = yosys_netlist_reader::extract_source_snippet(ep.driver_src, ep.clean_net, netlist_dir_, 1);
						if (!snip.empty()) {
							meta += std::format("`{}` | `{}`", drv_loc, snip);
						} else {
							meta += std::format("`{}`", drv_loc);
						}
					} else {
						meta += std::format("`{}`", drv_loc);
					}
				}
				if (ep.fanout > 0) {
					if (!meta.empty()) meta += ", ";
					meta += std::format("{} sink{}", ep.fanout, ep.fanout == 1 ? "" : "s");
				}
				md += std::format("- **Final Net**: `{}`{}\n", ep.clean_net, meta.empty() ? "" : std::format(" ({})", meta));
			}
			std::string ep_loc = clean_display_location(ep.end_src);
			std::string end_meta;
			if (!ep_loc.empty()) {
				if (show_code == 1 && !yosys_netlist_reader::is_library_source(ep.end_src)) {
					std::string snip = yosys_netlist_reader::extract_source_snippet(ep.end_src, ep.endpoint, netlist_dir_, 1);
					if (!snip.empty()) {
						end_meta += std::format("`{}` | `{}`", ep_loc, snip);
					} else {
						end_meta += std::format("`{}`", ep_loc);
					}
				} else {
					end_meta += std::format("`{}`", ep_loc);
				}
			}
			if (!ep.port.empty()) {
				if (!end_meta.empty()) end_meta += ", ";
				end_meta += std::format("port `{}`", ep.port);
			}
			md += std::format("- **End**: `{}`{}\n\n", ep.endpoint, end_meta.empty() ? "" : std::format(" ({})", end_meta));
		}
	};

	if (group_by_sink) {
		auto bottlenecks = get_sink_grouped_bottlenecks(reg_to_reg_only, top_k);
		if (!bottlenecks.empty()) {
			md += "## Physical Critical Paths (Grouped by Bottleneck Sink)\n\n";

			const auto &top = bottlenecks.front();
			double logic_pct = (top.worst_delay_ns > 0.0) ? (top.worst_logic_delay_ns / top.worst_delay_ns) * 100.0 : 0.0;
			double routing_pct = (top.worst_delay_ns > 0.0) ? (top.worst_routing_delay_ns / top.worst_delay_ns) * 100.0 : 0.0;

			md += std::format("**Critical Path Summary**: **{:.3f} ns** ({:.3f} ns logic [{:.0f}%], {:.3f} ns routing [{:.0f}%]), converging on `{}` from {} source signals\n\n",
			                  top.worst_delay_ns, top.worst_logic_delay_ns, logic_pct, top.worst_routing_delay_ns, routing_pct,
			                  top.endpoint, top.sources.size());

			std::set<std::string> printed_endpoints;
			for (size_t i = 0; i < bottlenecks.size(); ++i) {
				const auto &b = bottlenecks[i];
				printed_endpoints.insert(b.endpoint);
				double b_l_pct = (b.worst_delay_ns > 0.0) ? (b.worst_logic_delay_ns / b.worst_delay_ns) * 100.0 : 0.0;
				double b_r_pct = (b.worst_delay_ns > 0.0) ? (b.worst_routing_delay_ns / b.worst_delay_ns) * 100.0 : 0.0;

				md += std::format("### Path #{} [Reg -> Reg] Delay: **{:.3f} ns** ({:.0f}% logic, {:.0f}% routing) — Converging on `{}`\n",
				                  i + 1, b.worst_delay_ns, b_l_pct, b_r_pct, b.endpoint);

				if (b.sources.size() > 1) {
					std::string src_list;
					for (size_t s = 0; s < b.sources.size(); ++s) {
						if (s > 0) src_list += ", ";
						src_list += std::format("`{}`", b.sources[s]);
					}
					md += std::format("- **Sources** ({} source signals, all converge here): {}\n", b.sources.size(), src_list);
				} else if (b.sources.size() == 1) {
					md += std::format("- **Start**: `{}`\n", b.sources.front());
				}

				md += "- **Chain**:\n";
				md += format_hops_chain(b.representative_hops);
				if (show_segments) {
					md += format_hops_segments(b.representative_hops);
				}
				md += format_dominant_routing(b.representative_hops);
				std::string b_end_loc = format_end_loc(b.end_src, b.endpoint);
				md += std::format("- **End**: `{}` {}\n\n", b.endpoint, b_end_loc);
			}
			append_additional_endpoints(bottlenecks.size(), printed_endpoints);
		} else {
			md += "*No paths found matching filter criteria.*\n";
		}
	} else {
		auto paths = get_filtered_paths(reg_to_reg_only, top_k);
		if (!paths.empty()) {
			md += "## Physical Critical Paths\n\n";

			const auto &top = paths.front();
			double logic_pct = (top.total_delay_ns > 0.0) ? (top.logic_delay_ns / top.total_delay_ns) * 100.0 : 0.0;
			double routing_pct = (top.total_delay_ns > 0.0) ? (top.routing_delay_ns / top.total_delay_ns) * 100.0 : 0.0;

			md += std::format("**Critical Path Summary**: **{:.3f} ns** ({:.3f} ns logic [{:.0f}%], {:.3f} ns routing [{:.0f}%]) from `{}` to `{}`\n\n",
			                  top.total_delay_ns, top.logic_delay_ns, logic_pct, top.routing_delay_ns, routing_pct,
			                  top.startpoint, top.endpoint);

			std::set<std::string> printed_endpoints;
			for (size_t i = 0; i < paths.size(); ++i) {
				const auto &p = paths[i];
				printed_endpoints.insert(p.endpoint);
				double p_l_pct = (p.total_delay_ns > 0.0) ? (p.logic_delay_ns / p.total_delay_ns) * 100.0 : 0.0;
				double p_r_pct = (p.total_delay_ns > 0.0) ? (p.routing_delay_ns / p.total_delay_ns) * 100.0 : 0.0;

				md += std::format("### Path #{} [Reg -> Reg] Delay: **{:.3f} ns** ({:.0f}% logic, {:.0f}% routing)\n",
				                  i + 1, p.total_delay_ns, p_l_pct, p_r_pct);
				std::string p_start_loc = format_end_loc(p.start_src, p.startpoint);
				md += std::format("- **Start**: `{}` {}\n", p.startpoint, p_start_loc);
				md += "- **Chain**:\n";
				md += format_hops_chain(p.hops);
				if (show_segments) {
					md += format_hops_segments(p.hops);
				}
				md += format_dominant_routing(p.hops);
				std::string p_end_loc = format_end_loc(p.end_src, p.endpoint);
				md += std::format("- **End**: `{}` {}\n\n", p.endpoint, p_end_loc);
			}
			append_additional_endpoints(paths.size(), printed_endpoints);
		} else {
			md += "*No paths found matching filter criteria.*\n";
		}
	}

	return md;
}

} // namespace verilog
