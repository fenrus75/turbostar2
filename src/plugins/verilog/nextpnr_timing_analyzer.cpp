#include "plugins/verilog/nextpnr_timing_analyzer.h"
#include "plugins/verilog/yosys_netlist_reader.h"
#include "fs_utils.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <algorithm>
#include <regex>
#include <map>
#include <set>

namespace verilog {

namespace fs = std::filesystem;

bool nextpnr_timing_analyzer::load_from_files(std::string_view timing_json_path, std::string_view netlist_json_path, std::string &out_error)
{
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
	netlist_ports_.clear();
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
		}
	}

	if (mod_obj->contains("netnames") && (*mod_obj)["netnames"].is_object()) {
		for (const auto &[nname, ndata] : (*mod_obj)["netnames"].items()) {
			net_info ni;
			if (ndata.contains("attributes") && ndata["attributes"].is_object()) {
				ni.src_raw = ndata["attributes"].value("src", "");
			}
			netlist_nets_[nname] = std::move(ni);
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
	}

	// Netname prefix lookup
	size_t best_len = 0;
	std::string best_net;
	std::string net_src;
	for (const auto &[nname, ndata] : netlist_nets_) {
		std::string cleaned_n_src = yosys_netlist_reader::clean_src_location(ndata.src_raw);
		if (cleaned_n_src.empty()) continue;

		if (cand.starts_with(nname) && nname.size() > best_len) {
			if (cand.size() == nname.size() || cand[nname.size()] == '_' || cand[nname.size()] == '.') {
				best_len = nname.size();
				best_net = nname;
				net_src = cleaned_n_src;
			}
		}
	}

	if (!best_net.empty()) {
		out_hint = best_net;
		if (src.empty() || (yosys_netlist_reader::is_library_source(src) && !yosys_netlist_reader::is_library_source(net_src))) {
			src = net_src;
		}
	} else if (!matched_cell.empty()) {
		out_hint = matched_cell;
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

		cp.end_src = resolve_cell_src(cp.endpoint, hint);
		if (!hint.empty() && hint != normalize_cell_name(cp.endpoint)) {
			cp.endpoint = hint;
		}

		for (auto &h : cp.hops) {
			h.cell_src_location = resolve_cell_src(h.to_cell.empty() ? h.from_cell : h.to_cell, h.cell_hint);
			h.net_src_location = resolve_net_src(h.net_name);
			if ((h.cell_src_location.empty() || yosys_netlist_reader::is_library_source(h.cell_src_location)) &&
			    !h.net_src_location.empty() && !yosys_netlist_reader::is_library_source(h.net_src_location)) {
				h.cell_src_location = h.net_src_location;
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

std::string nextpnr_timing_analyzer::generate_timing_markdown(bool reg_to_reg_only, bool group_by_sink, size_t top_k) const
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

	auto format_hops_chain = [](std::span<const timing_hop> hops) -> std::string {
		struct stage {
			std::string src_location;
			std::string hint;
			double logic_delay{0.0};
			double routing_delay{0.0};
			size_t luts{0};
			std::string out_net;
		};
		std::vector<stage> stages;

		for (const auto &h : hops) {
			if (h.type == "source" || h.type == "setup") continue;

			std::string loc = h.cell_src_location;
			if ((loc.empty() || yosys_netlist_reader::is_library_source(loc)) &&
			    !h.net_src_location.empty() && !yosys_netlist_reader::is_library_source(h.net_src_location)) {
				loc = h.net_src_location;
			}

			if (!stages.empty() && !loc.empty() && stages.back().src_location == loc) {
				if (h.type == "logic" || h.type == "clk-to-q") {
					stages.back().logic_delay += h.delay_ns;
					stages.back().luts++;
				} else if (h.type == "routing") {
					stages.back().routing_delay += h.delay_ns;
					if (!h.net_name.empty()) stages.back().out_net = h.net_name;
				}
			} else {
				stage st;
				st.src_location = loc;
				st.hint = h.cell_hint;
				if (h.type == "logic" || h.type == "clk-to-q") {
					st.logic_delay = h.delay_ns;
					st.luts = 1;
				} else if (h.type == "routing") {
					st.routing_delay = h.delay_ns;
					st.out_net = h.net_name;
				}
				stages.push_back(std::move(st));
			}
		}

		std::string chain_md;
		for (size_t s_idx = 0; s_idx < stages.size(); ++s_idx) {
			const auto &st = stages[s_idx];
			std::string loc_str = st.src_location.empty() ? "[interconnect]" : std::format("`{}`", st.src_location);
			std::string hint_str = st.hint.empty() ? "" : std::format(" ({})", st.hint);
			std::string out_str = (!st.out_net.empty() && st.out_net.find('$') == std::string::npos) ? std::format(" -> `{}`", st.out_net) : "";

			chain_md += std::format("  {}. {}{} — **{:.3f} ns** ({:.3f} ns logic, {:.3f} ns routing){}\n",
			                        s_idx + 1, loc_str, hint_str,
			                        st.logic_delay + st.routing_delay, st.logic_delay, st.routing_delay, out_str);
		}
		return chain_md;
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
				md += std::format("- **End**: `{}` {}\n\n", b.endpoint, b.end_src.empty() ? "" : std::format("(`{}`)", b.end_src));
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
				md += std::format("- **Start**: `{}` {}\n", p.startpoint, p.start_src.empty() ? "" : std::format("(`{}`)", p.start_src));
				md += "- **Chain**:\n";
				md += format_hops_chain(p.hops);
				md += std::format("- **End**: `{}` {}\n\n", p.endpoint, p.end_src.empty() ? "" : std::format("(`{}`)", p.end_src));
			}
		} else {
			md += "*No paths found matching filter criteria.*\n";
		}
	}

	return md;
}

} // namespace verilog
