#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/yosys_netlist_reader.h"
#include "fs_utils.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <algorithm>
#include <charconv>
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
		netlist_dir_ = fs::path(netlist_json_path).parent_path().string();
		std::ifstream n_file{std::string(netlist_json_path)};
		if (n_file.is_open()) {
			try {
				nlohmann::json n_json;
				n_file >> n_json;
				parse_netlist_json(n_json);
			} catch (...) {
			}
		}
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

	std::map<int, std::string> bit_driver_file;
	auto extract_src_file = [](std::string_view raw) -> std::string {
		if (raw.empty()) {
			return "";
		}
		size_t bar = raw.find('|');
		std::string_view first = (bar != std::string_view::npos) ? raw.substr(0, bar) : raw;
		size_t colon = first.rfind(':');
		if (colon != std::string_view::npos) {
			return std::string(first.substr(0, colon));
		}
		return std::string(first);
	};

	if (mod_obj->contains("cells") && (*mod_obj)["cells"].is_object()) {
		for (const auto &[cname, cdata] : (*mod_obj)["cells"].items()) {
			cell_info ci;
			ci.type = cdata.value("type", "");
			if (cdata.contains("attributes") && cdata["attributes"].is_object()) {
				ci.src_raw = cdata["attributes"].value("src", "");
			}
			std::string c_file = extract_src_file(ci.src_raw);
			bool valid_c_file = !c_file.empty() && !yosys_netlist_reader::is_library_source(c_file);
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

					if (conn_val.is_array()) {
						for (const auto &b_val : conn_val) {
							if (b_val.is_number_integer()) {
								int b = b_val.get<int>();
								if (b >= 0) {
									if (is_input) {
										bit_sink_counts_[b]++;
									} else if (valid_c_file) {
										bit_driver_file[b] = c_file;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	std::map<std::vector<int>, std::vector<std::pair<std::string, std::string>>> bits_to_user_nets;
	if (mod_obj->contains("netnames") && (*mod_obj)["netnames"].is_object()) {
		for (const auto &[nname, ndata] : (*mod_obj)["netnames"].items()) {
			net_info ni;
			int hide_name = ndata.value("hide_name", 0);
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
			bool is_synth = (nname.find("_SB_") != std::string::npos ||
			                 nname.find("$SB_") != std::string::npos ||
			                 nname.find("$sb_") != std::string::npos ||
			                 nname.find("$func$") != std::string::npos);
			std::string src_file = extract_src_file(ni.src_raw);
			bool is_lib = yosys_netlist_reader::is_library_source(src_file);
			if (hide_name == 0 && !nname.starts_with("$") && !nname.starts_with("new_n") && !is_synth && !is_lib && !ni.bits.empty()) {
				bits_to_user_nets[ni.bits].emplace_back(nname, src_file);
			}
			netlist_nets_[nname] = std::move(ni);
		}
	}

	for (const auto &[bits, candidates] : bits_to_user_nets) {
		if (candidates.size() <= 1) {
			continue;
		}
		std::string drv_file;
		for (int b : bits) {
			if (b > 0 && bit_driver_file.contains(b)) {
				drv_file = bit_driver_file[b];
				break;
			}
		}
		auto score_cand = [&](const std::pair<std::string, std::string> &cand) {
			int driver_match = (!drv_file.empty() && cand.second == drv_file) ? 0 : 1;
			size_t dots = static_cast<size_t>(std::count(cand.first.begin(), cand.first.end(), '.'));
			return std::make_tuple(driver_match, dots, cand.first.size(), cand.first);
		};
		auto best_it = std::min_element(candidates.begin(), candidates.end(),
			[&](const auto &a, const auto &b) {
				return score_cand(a) < score_cand(b);
			});
		const std::string &best_name = best_it->first;
		for (const auto &[cand_name, cand_file] : candidates) {
			net_alias_map_[cand_name] = best_name;
		}
	}
}

void nextpnr_timing_analyzer::parse_timing_json(const nlohmann::json &j)
{
	clock_domains_.clear();
	utilization_.clear();
	critical_paths_.clear();

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

					if (hop_obj.contains("from") && hop_obj["from"].is_object()) {
						h.from_cell = hop_obj["from"].value("cell", "");
						h.from_port = hop_obj["from"].value("port", "");
					}
					if (hop_obj.contains("to") && hop_obj["to"].is_object()) {
						h.to_cell = hop_obj["to"].value("cell", "");
						h.to_port = hop_obj["to"].value("port", "");
					}

					if (hop_obj.contains("from") && hop_obj["from"].is_object() &&
					    hop_obj.contains("to") && hop_obj["to"].is_object()) {
						const auto &f_obj = hop_obj["from"];
						const auto &t_obj = hop_obj["to"];
						if (f_obj.contains("loc") && f_obj["loc"].is_array() && f_obj["loc"].size() == 2 &&
						    t_obj.contains("loc") && t_obj["loc"].is_array() && t_obj["loc"].size() == 2) {
							if (f_obj["loc"][0].is_number_integer() && f_obj["loc"][1].is_number_integer() &&
							    t_obj["loc"][0].is_number_integer() && t_obj["loc"][1].is_number_integer()) {
								int x1 = f_obj["loc"][0].get<int>();
								int y1 = f_obj["loc"][1].get<int>();
								int x2 = t_obj["loc"][0].get<int>();
								int y2 = t_obj["loc"][1].get<int>();
								h.distance = std::abs(x1 - x2) + std::abs(y1 - y2);
							}
						}
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

std::string nextpnr_timing_analyzer::resolve_cell_src(std::string_view raw_cell, std::string &out_hint) const
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

	std::string src;
	if (!matched_cell.empty()) {
		const auto &ci = netlist_cells_.at(matched_cell);
		src = yosys_netlist_reader::clean_src_location(ci.src_raw);
		if (ci.type.find("DFF") != std::string::npos) {
			std::string inferred = yosys_netlist_reader::infer_register_from_src(ci.src_raw, cand, netlist_dir_);
			if (!inferred.empty()) {
				out_hint = resolve_alias(normalize_net_name(inferred));
				return src;
			}
		}
	}

	// Netname prefix lookup (prefer clean user nets, skip synthetic LUT/techmap intermediates)
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

		if (cand.starts_with(nname) && nname.size() > best_len) {
			if (cand.size() == nname.size() || cand[nname.size()] == '_' || cand[nname.size()] == '.') {
				best_len = nname.size();
				best_net = nname;
				net_src = cleaned_n_src;
			}
		}
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
		cp.start_src = resolve_cell_src(cp.startpoint, hint);
		if (!hint.empty() && hint != normalize_cell_name(cp.startpoint)) {
			cp.startpoint = hint;
		}
		cp.startpoint = resolve_alias(normalize_net_name(cp.startpoint));

		cp.end_src = resolve_cell_src(cp.endpoint, hint);
		if (!hint.empty() && hint != normalize_cell_name(cp.endpoint)) {
			cp.endpoint = hint;
		}
		cp.endpoint = resolve_alias(normalize_net_name(cp.endpoint));

		for (auto &h : cp.hops) {
			h.cell_src_location = resolve_cell_src(h.to_cell.empty() ? h.from_cell : h.to_cell, h.cell_hint);
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

bool nextpnr_timing_analyzer::synthesize_and_route(const fpga_target_config &cfg, std::span<const std::string> sources, std::string &out_error)
{
	std::string tmp_dir = fs_utils::get_project_tmp_dir();
	fs::path synth_json = fs::path(tmp_dir) / "pnr_synth.json";
	fs::path timing_json = fs::path(tmp_dir) / "pnr_timing.json";
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

	// 2. Run nextpnr
	std::string pnr_tool = (cfg.target == "ice40") ? "nextpnr-ice40" :
	                       (cfg.target == "ecp5") ? "nextpnr-ecp5" : "nextpnr-generic";

	std::string pnr_args = "--json " + fs_utils::escape_shell_arg(synth_json.string()) +
	                       " --report " + fs_utils::escape_shell_arg(timing_json.string()) +
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

	std::string full_pnr_cmd = pnr_tool + " " + pnr_args;
	std::string p_out = fs_utils::execute_command_sync(full_pnr_cmd);
	if (!fs::exists(timing_json)) {
		out_error = std::format("nextpnr place-and-route failed to generate timing report:\n{}", p_out);
		return false;
	}

	return load_from_files(timing_json.string(), synth_json.string(), out_error);
}

std::string nextpnr_timing_analyzer::generate_timing_markdown(bool reg_to_reg_only, bool group_by_sink, size_t top_k, bool collapse) const
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
			md += std::format("| `{}` | {:.2f} MHz | **{:.2f} MHz** | {} | {} |\n",
			                  cd.domain_name, cd.constraint_mhz, cd.achieved_mhz, slack_str, status);
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

	auto format_hops_chain = [collapse](std::span<const timing_hop> hops) -> std::string {
		struct stage {
			std::string raw_src;
			std::string display_loc;
			std::string base_hint;
			std::string first_cell;
			std::string last_cell;
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
				if (back.last_cell == from_cell || back.first_cell == from_cell || back.last_cell == hop_cell) {
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
				st.last_cell = to_cell;
				if (st.base_hint.empty() && !effective_base.empty()) {
					st.base_hint = effective_base;
				}
				if (h.type == "logic" || h.type == "clk-to-q") {
					st.logic_delay += h.delay_ns;
					st.luts++;
				} else if (h.type == "routing") {
					st.routing_delay += h.delay_ns;
					if (!norm_net.empty()) {
						st.out_net = norm_net;
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

			chain_md += std::format("  {}. {}{} {}{}\n",
			                        s_idx + 1, loc_str, hint_str, delay_str, out_str);
		}
		return chain_md;
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

			for (size_t i = 0; i < bottlenecks.size(); ++i) {
				const auto &b = bottlenecks[i];
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
				md += format_dominant_routing(b.representative_hops);
				std::string b_end_src = clean_display_location(b.end_src);
				md += std::format("- **End**: `{}` {}\n\n", b.endpoint, b_end_src.empty() ? "" : std::format("(`{}`)", b_end_src));
			}
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

			for (size_t i = 0; i < paths.size(); ++i) {
				const auto &p = paths[i];
				double p_l_pct = (p.total_delay_ns > 0.0) ? (p.logic_delay_ns / p.total_delay_ns) * 100.0 : 0.0;
				double p_r_pct = (p.total_delay_ns > 0.0) ? (p.routing_delay_ns / p.total_delay_ns) * 100.0 : 0.0;

				md += std::format("### Path #{} [Reg -> Reg] Delay: **{:.3f} ns** ({:.0f}% logic, {:.0f}% routing)\n",
				                  i + 1, p.total_delay_ns, p_l_pct, p_r_pct);
				std::string p_start_src = clean_display_location(p.start_src);
				md += std::format("- **Start**: `{}` {}\n", p.startpoint, p_start_src.empty() ? "" : std::format("(`{}`)", p_start_src));
				md += "- **Chain**:\n";
				md += format_hops_chain(p.hops);
				md += format_dominant_routing(p.hops);
				std::string p_end_src = clean_display_location(p.end_src);
				md += std::format("- **End**: `{}` {}\n\n", p.endpoint, p_end_src.empty() ? "" : std::format("(`{}`)", p_end_src));
			}
		} else {
			md += "*No paths found matching filter criteria.*\n";
		}
	}

	return md;
}

} // namespace verilog
