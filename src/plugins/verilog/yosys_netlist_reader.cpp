#include "yosys_netlist_reader.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <format>
#include <algorithm>
#include <ranges>

namespace verilog {

namespace {

std::string describe_cell_type(std::string_view type)
{
	if (type == "$adff" || type == "$dff" || type == "$sdff" || type == "$dffe") {
		return "sequential register";
	}
	if (type == "$mux" || type == "$pmux") {
		return "multiplexer";
	}
	if (type == "$add" || type == "$sub" || type == "$mul") {
		return "arithmetic ALU";
	}
	if (type == "$and" || type == "$or" || type == "$xor" || type == "$not" || type == "$logic_and" || type == "$logic_not") {
		return "logic gate";
	}
	if (type == "$eq" || type == "$ne" || type == "$lt" || type == "$le" || type == "$gt" || type == "$ge") {
		return "comparator";
	}
	if (type == "$reduce_or" || type == "$reduce_and" || type == "$reduce_xor") {
		return "reduction gate";
	}
	return std::string(type);
}

std::string extract_module_prefix(std::string_view name)
{
	// Yosys cell name e.g. "$flatten\cpu8008.\alu.$add$sv/alu/alu.sv:28$4"
	// or "cpu8008.alu"
	std::string s(name);
	if (s.starts_with("$flatten\\")) {
		s.erase(0, 9);
	}
	// Remove trailing raw cell parts after .
	// Look for last non-dollar segment
	std::string res;
	std::stringstream ss(s);
	std::string token;
	while (std::getline(ss, token, '.')) {
		if (token.starts_with('$')) break;
		if (token.ends_with('\\')) token.pop_back();
		if (!token.empty()) {
			if (!res.empty()) res += ".";
			res += token;
		}
	}
	return res.empty() ? std::string(name) : res;
}

} // anonymous namespace

bool yosys_netlist_reader::is_library_source(std::string_view s)
{
	if (s.empty()) return false;
	return s.find("cells_map.v") != std::string_view::npos ||
	       s.find("cells_sim.v") != std::string_view::npos ||
	       s.find("arith_map.v") != std::string_view::npos ||
	       s.find("brams_map.v") != std::string_view::npos ||
	       s.find("lutrams_map.v") != std::string_view::npos ||
	       s.find("share/yosys") != std::string_view::npos ||
	       s.find("techmap.v") != std::string_view::npos ||
	       s.find("simlib.v") != std::string_view::npos ||
	       s.find("simcells.v") != std::string_view::npos;
}

std::string yosys_netlist_reader::normalize_net_name(std::string_view raw_net)
{
	if (raw_net.empty()) return "";

	std::string s(raw_net);

	// Find the earliest synthetic techmap or function-inlining marker.
	// Note: Any trailing [i] appearing *after* _SB_ (e.g. D_reg.load_SB_LUT4_O_I2[3])
	// is a cells_map.v LUT input port index (A[3:0]), NOT a bit index of the RTL signal.
	// Any real bit index appearing *before* $SB_ (e.g. LED[3]$SB_IO_OUT) is naturally preserved.
	size_t best_pos = std::string::npos;
	for (const auto &marker : {"_SB_", "$SB_", "$sb_", "$func$"}) {
		size_t pos = s.find(marker);
		if (pos != std::string::npos && pos > 0) {
			if (best_pos == std::string::npos || pos < best_pos) {
				best_pos = pos;
			}
		}
	}

	if (best_pos != std::string::npos) {
		s = s.substr(0, best_pos);
	}

	while (s.size() > 1 && (s.back() == '_' || s.back() == '.')) {
		s.pop_back();
	}

	return s;
}

std::string yosys_netlist_reader::infer_register_from_src(std::string_view raw_src, std::string_view raw_cell_name, std::string_view base_dir)
{
	std::string cell_str(raw_cell_name);
	for (const auto &sfx : {"_DFFLC", "_LC", "_RAM", "$sb_io"}) {
		if (cell_str.ends_with(sfx)) {
			cell_str.resize(cell_str.size() - std::string_view(sfx).size());
			break;
		}
	}

	std::string norm_base = normalize_net_name(cell_str);
	size_t last_dot = norm_base.rfind('.');
	std::string scope = (last_dot != std::string::npos) ? norm_base.substr(0, last_dot) : "";
	std::string leaf = (last_dot != std::string::npos) ? norm_base.substr(last_dot + 1) : norm_base;

	// Extract first non-library source segment e.g. "sv/alu/alu.sv:19.2-24.5"
	std::string_view clean_seg;
	size_t start = 0;
	while (start < raw_src.size()) {
		size_t bar = raw_src.find('|', start);
		std::string_view seg = (bar == std::string_view::npos) ? raw_src.substr(start) : raw_src.substr(start, bar - start);
		if (!seg.empty() && !is_library_source(seg) && !seg.ends_with(":0.0-0.0")) {
			clean_seg = seg;
			break;
		}
		if (bar == std::string_view::npos) break;
		start = bar + 1;
	}

	std::set<std::string> lhs_idents;
	if (!clean_seg.empty()) {
		size_t colon = clean_seg.rfind(':');
		if (colon != std::string_view::npos && colon > 0 && colon + 1 < clean_seg.size()) {
			std::string file_part(clean_seg.substr(0, colon));
			std::string_view range_part = clean_seg.substr(colon + 1);
			size_t dash = range_part.find('-');
			auto parse_line = [](std::string_view tok) -> int {
				size_t dot = tok.find('.');
				std::string s(dot != std::string_view::npos ? tok.substr(0, dot) : tok);
				try { return std::stoi(s); } catch (...) { return 0; }
			};
			int sline = parse_line(dash != std::string_view::npos ? range_part.substr(0, dash) : range_part);
			int eline = (dash != std::string_view::npos) ? parse_line(range_part.substr(dash + 1)) : sline;

			if (sline > 0 && eline >= sline) {
				std::filesystem::path resolved_path;
				std::vector<std::filesystem::path> search_Attempt = { std::filesystem::path(file_part) };
				if (!base_dir.empty()) {
					std::filesystem::path bp(base_dir);
					search_Attempt.push_back(bp / file_part);
					if (bp.has_parent_path()) {
						search_Attempt.push_back(bp.parent_path() / file_part);
						if (bp.parent_path().has_parent_path()) {
							search_Attempt.push_back(bp.parent_path().parent_path() / file_part);
						}
					}
				}
				for (const auto &cand_p : search_Attempt) {
					std::error_code ec;
					if (std::filesystem::exists(cand_p, ec) && std::filesystem::is_regular_file(cand_p, ec)) {
						resolved_path = cand_p;
						break;
					}
				}

				if (!resolved_path.empty()) {
					std::ifstream ifs(resolved_path);
					std::string line;
					int cur_line = 0;
					while (cur_line < eline && std::getline(ifs, line)) {
						cur_line++;
						if (cur_line < sline) continue;
						size_t cmt = line.find("//");
						if (cmt != std::string::npos) line.resize(cmt);

						size_t pos = 0;
						while ((pos = line.find("<=", pos)) != std::string::npos) {
							// Check balanced parentheses before '<=' on this line to skip 'if (a <= b)'
							int paren_depth = 0;
							for (size_t i = 0; i < pos; ++i) {
								if (line[i] == '(') paren_depth++;
								else if (line[i] == ')' && paren_depth > 0) paren_depth--;
							}
							if (paren_depth == 0 && pos > 0) {
								size_t p = pos;
								while (p > 0 && (line[p - 1] == ' ' || line[p - 1] == '\t')) p--;
								// Skip any array/bit indexing [ ... ] on the LHS
								while (p > 0 && line[p - 1] == ']') {
									int brk = 1;
									p--;
									while (p > 0 && brk > 0) {
										p--;
										if (line[p] == ']') brk++;
										else if (line[p] == '[') brk--;
									}
									while (p > 0 && (line[p - 1] == ' ' || line[p - 1] == '\t')) p--;
								}
								size_t id_end = p;
								while (p > 0 && (std::isalnum(static_cast<unsigned char>(line[p - 1])) || line[p - 1] == '_')) {
									p--;
								}
								if (id_end > p) {
									std::string ident = line.substr(p, id_end - p);
									if (!ident.empty() && !std::isdigit(static_cast<unsigned char>(ident[0]))) {
										lhs_idents.insert(std::move(ident));
									}
								}
							}
							pos += 2;
						}
					}
				}
			}
		}
	}

	if (lhs_idents.size() == 1) {
		const std::string &reg = *lhs_idents.begin();
		return scope.empty() ? reg : std::format("{}.{}", scope, reg);
	}

	if (leaf.starts_with("next_") && leaf.size() > 5) {
		std::string cand_reg = leaf.substr(5);
		size_t sb_pos = cell_str.find("_SB_");
		bool direct_dff = (sb_pos != std::string::npos && std::string_view(cell_str).substr(sb_pos).starts_with("_SB_DFF"));
		if (lhs_idents.contains(cand_reg) || (lhs_idents.empty() && direct_dff)) {
			return scope.empty() ? cand_reg : std::format("{}.{}", scope, cand_reg);
		}
	}

	if (!leaf.empty() && lhs_idents.contains(leaf)) {
		return scope.empty() ? leaf : std::format("{}.{}", scope, leaf);
	}

	return "";
}

std::string yosys_netlist_reader::clean_display_location(std::string_view loc)
{
	if (loc.empty()) return "";
	if (is_library_source(loc)) {
		size_t colon = loc.find(':');
		std::string_view file_part = (colon != std::string_view::npos) ? loc.substr(0, colon) : loc;
		return std::filesystem::path(file_part).filename().string();
	}
	return std::string(loc);
}
std::string yosys_netlist_reader::clean_src_location(std::string_view raw_src)
{
	if (raw_src.empty()) return "";

	// Handle multi-pipe locations like "sv/alu/alu.sv:0.0-0.0|sv/alu/alu.sv:24.3-53.10"
	std::vector<std::string_view> segments;
	size_t start = 0;
	while (start < raw_src.size()) {
		size_t pos = raw_src.find('|', start);
		if (pos == std::string_view::npos) {
			segments.push_back(raw_src.substr(start));
			break;
		}
		segments.push_back(raw_src.substr(start, pos - start));
		start = pos + 1;
	}

	std::string_view chosen = segments.front();
	std::string_view non_lib_choice;
	std::string_view any_valid_choice;
	for (const auto &seg : segments) {
		if (seg.find(":0.0-0.0") == std::string_view::npos && seg.find(":0") == std::string_view::npos) {
			if (any_valid_choice.empty()) {
				any_valid_choice = seg;
			}
			if (!is_library_source(seg)) {
				non_lib_choice = seg;
				break;
			}
		}
	}
	if (!non_lib_choice.empty()) {
		chosen = non_lib_choice;
	} else if (!any_valid_choice.empty()) {
		chosen = any_valid_choice;
	}

	// Format: file.sv:start_line.start_col-end_line.end_col
	size_t colon_pos = chosen.rfind(':');
	if (colon_pos == std::string_view::npos) {
		return std::string(chosen);
	}

	std::string file_part(chosen.substr(0, colon_pos));
	std::string_view range_part = chosen.substr(colon_pos + 1);

	int start_line = 0;
	int end_line = 0;

	// Parse start_line (up to '.' or '-')
	size_t idx = 0;
	while (idx < range_part.size() && std::isdigit(static_cast<unsigned char>(range_part[idx]))) {
		start_line = start_line * 10 + (range_part[idx] - '0');
		++idx;
	}

	// Look for '-' to find end_line
	size_t dash_pos = range_part.find('-', idx);
	if (dash_pos != std::string_view::npos) {
		size_t end_idx = dash_pos + 1;
		while (end_idx < range_part.size() && std::isdigit(static_cast<unsigned char>(range_part[end_idx]))) {
			end_line = end_line * 10 + (range_part[end_idx] - '0');
			++end_idx;
		}
	} else {
		end_line = start_line;
	}

	if (start_line == 0) {
		return file_part;
	}
	if (start_line == end_line || end_line == 0) {
		return std::format("{}:{}", file_part, start_line);
	}
	return std::format("{}:{}-{}", file_part, start_line, end_line);
}

bool yosys_netlist_reader::load_from_file(std::string_view netlist_path, std::string &out_error)
{
	std::ifstream file{std::string(netlist_path)};
	if (!file.is_open()) {
		out_error = std::format("Failed to open netlist file: {}", netlist_path);
		return false;
	}

	try {
		nlohmann::json j;
		file >> j;

		if (!j.contains("modules") || !j["modules"].is_object()) {
			out_error = "Invalid Yosys netlist: missing 'modules' object";
			return false;
		}

		// Find the top module
		const nlohmann::json *chosen_module = nullptr;
		std::string chosen_module_name;

		for (const auto &[mod_name, mod_obj] : j["modules"].items()) {
			if (mod_obj.contains("attributes") && mod_obj["attributes"].contains("top")) {
				chosen_module = &mod_obj;
				chosen_module_name = mod_name;
				break;
			}
		}

		if (!chosen_module && !j["modules"].empty()) {
			auto it = j["modules"].begin();
			chosen_module = &it.value();
			chosen_module_name = it.key();
		}

		if (!chosen_module) {
			out_error = "No modules found in Yosys netlist";
			return false;
		}

		top_module_name_ = chosen_module_name;
		top_ports_.clear();
		netnames_.clear();
		cells_.clear();

		// Parse top ports
		if (chosen_module->contains("ports") && (*chosen_module)["ports"].is_object()) {
			for (const auto &[port_name, port_obj] : (*chosen_module)["ports"].items()) {
				top_ports_[port_name] = port_obj.value("direction", "inout");
			}
		}

		// Parse netnames
		if (chosen_module->contains("netnames") && (*chosen_module)["netnames"].is_object()) {
			for (const auto &[net_name, net_obj] : (*chosen_module)["netnames"].items()) {
				parsed_net net;
				net.name = net_name;

				if (net_obj.contains("bits") && net_obj["bits"].is_array()) {
					for (const auto &b : net_obj["bits"]) {
						if (b.is_number_integer()) {
							net.bits.push_back(b.get<int>());
						}
					}
				}

				if (net_obj.contains("attributes") && net_obj["attributes"].is_object()) {
					net.src_raw = net_obj["attributes"].value("src", "");
					net.hdlname = net_obj["attributes"].value("hdlname", "");
				}

				netnames_[net_name] = std::move(net);
			}
		}

		// Parse cells
		if (chosen_module->contains("cells") && (*chosen_module)["cells"].is_object()) {
			for (const auto &[cell_name, cell_obj] : (*chosen_module)["cells"].items()) {
				parsed_cell c;
				c.name = cell_name;
				c.type = cell_obj.value("type", "");

				if (cell_obj.contains("port_directions") && cell_obj["port_directions"].is_object()) {
					for (const auto &[pname, dir] : cell_obj["port_directions"].items()) {
						if (dir.is_string()) {
							c.port_directions[pname] = dir.get<std::string>();
						}
					}
				}

				if (cell_obj.contains("connections") && cell_obj["connections"].is_object()) {
					for (const auto &[pname, b_arr] : cell_obj["connections"].items()) {
						if (b_arr.is_array()) {
							for (const auto &b : b_arr) {
								if (b.is_number_integer()) {
									c.connections[pname].push_back(b.get<int>());
								}
							}
						}
					}
				}

				if (cell_obj.contains("attributes") && cell_obj["attributes"].is_object()) {
					c.src_raw = cell_obj["attributes"].value("src", "");
				}

				cells_.push_back(std::move(c));
			}
		}

		return true;
	} catch (const std::exception &ex) {
		out_error = std::format("Exception parsing Yosys netlist JSON: {}", ex.what());
		return false;
	}
}

std::optional<signal_source_trace> yosys_netlist_reader::trace_signal(std::string_view query_signal) const
{
	if (query_signal.empty() || netnames_.empty()) {
		return std::nullopt;
	}

	// 1. Match query signal against netnames
	const parsed_net *best_net = nullptr;
	std::string best_net_name;
	size_t best_match_score = 0;

	// Check exact match
	auto it = netnames_.find(std::string(query_signal));
	if (it != netnames_.end()) {
		best_net = &it->second;
		best_net_name = it->first;
		best_match_score = 10000;
	} else {
		// Suffix match: e.g. "tb_main.uut.cpu8008.A_val" matches "cpu8008.A_val"
		for (const auto &[name, net] : netnames_) {
			if (query_signal.ends_with("." + name)) {
				size_t score = name.size() * 10;
				if (score > best_match_score) {
					best_match_score = score;
					best_net = &net;
					best_net_name = name;
				}
			} else if (name.ends_with("." + std::string(query_signal))) {
				size_t score = query_signal.size() * 8;
				if (score > best_match_score) {
					best_match_score = score;
					best_net = &net;
					best_net_name = name;
				}
			}
		}

		// Leaf match: e.g. "A_val" matches "cpu8008.A_val"
		if (!best_net) {
			std::string_view query_leaf = query_signal;
			size_t last_dot = query_leaf.rfind('.');
			if (last_dot != std::string_view::npos) {
				query_leaf = query_leaf.substr(last_dot + 1);
			}

			for (const auto &[name, net] : netnames_) {
				std::string_view net_leaf = name;
				size_t d = net_leaf.rfind('.');
				if (d != std::string_view::npos) {
					net_leaf = net_leaf.substr(d + 1);
				}
				if (net_leaf == query_leaf) {
					best_net = &net;
					best_net_name = name;
					break;
				}
			}
		}
	}

	if (!best_net || best_net->bits.empty()) {
		return std::nullopt;
	}

	signal_source_trace trace;
	trace.matched_net_name = best_net_name;
	trace.query_signal = std::string(query_signal);
	trace.bit_width = best_net->bits.size();
	trace.bit_ids = best_net->bits;

	std::set<int> target_bits(best_net->bits.begin(), best_net->bits.end());

	// Check if this net is a top-level port
	auto port_it = top_ports_.find(best_net_name);
	if (port_it != top_ports_.end()) {
		trace.is_top_port = true;
		trace.top_port_direction = port_it->second;
	}

	// 2. Discover all connected hierarchy nets sharing the same bits
	for (const auto &[name, net] : netnames_) {
		if (net.bits == best_net->bits) {
			net_declaration decl;
			decl.hierarchical_name = name;
			decl.src_location = clean_src_location(net.src_raw);

			// Determine role
			auto p_it = top_ports_.find(name);
			if (p_it != top_ports_.end()) {
				decl.role = std::format("top-level port ({})", p_it->second);
			} else if (name.ends_with(".value") || name.ends_with(".out") || name.find("_out") != std::string::npos) {
				decl.role = "module output port";
			} else if (name.ends_with(".clk") || name.ends_with(".rst") || name.find(".input") != std::string::npos || name.find("_in") != std::string::npos) {
				decl.role = "module input port";
			} else if (name.ends_with("_reg")) {
				decl.role = "internal register";
			} else {
				decl.role = "module wire / net";
			}

			trace.hierarchy_declarations.push_back(std::move(decl));
		}
	}

	// Sort declarations: shortest / shallowest hierarchy first
	std::ranges::sort(trace.hierarchy_declarations, [](const net_declaration &a, const net_declaration &b) {
		size_t dots_a = std::ranges::count(a.hierarchical_name, '.');
		size_t dots_b = std::ranges::count(b.hierarchical_name, '.');
		if (dots_a != dots_b) return dots_a < dots_b;
		return a.hierarchical_name < b.hierarchical_name;
	});

	// 3. Find drivers and consumers from cells
	struct raw_consumer {
		std::string module_prefix;
		std::string file_path;
		int line{0};
		std::string cell_type;
	};
	std::vector<raw_consumer> raw_consumers;

	for (const auto &cell : cells_) {
		std::string cell_src = clean_src_location(cell.src_raw);
		std::string mod_prefix = extract_module_prefix(cell.name);

		for (const auto &[pname, bits] : cell.connections) {
			// Check bit overlap
			bool overlap = false;
			for (int b : bits) {
				if (target_bits.contains(b)) {
					overlap = true;
					break;
				}
			}
			if (!overlap) continue;

			auto dir_it = cell.port_directions.find(pname);
			std::string dir = (dir_it != cell.port_directions.end()) ? dir_it->second : "unknown";

			if (dir == "output") {
				cell_driver d;
				d.cell_name = cell.name;
				d.cell_type = cell.type;
				d.port_name = pname;
				d.src_location = cell_src;
				d.description = std::format("{} ({}) port {}", describe_cell_type(cell.type), cell.type, pname);
				trace.drivers.push_back(std::move(d));
			} else if (dir == "input") {
				// Parse line and file from cell_src
				raw_consumer rc;
				rc.module_prefix = mod_prefix;
				rc.cell_type = cell.type;

				std::string_view cell_view = cell_src;
				size_t c_pos = cell_view.rfind(':');
				if (c_pos != std::string_view::npos) {
					rc.file_path = std::string(cell_view.substr(0, c_pos));
					std::string_view l_str = cell_view.substr(c_pos + 1);
					int l = 0;
					for (char ch : l_str) {
						if (std::isdigit(static_cast<unsigned char>(ch))) {
							l = l * 10 + (ch - '0');
						} else {
							break;
						}
					}
					rc.line = l;
				} else {
					rc.file_path = cell_src;
					rc.line = 0;
				}
				raw_consumers.push_back(std::move(rc));
			}
		}
	}

	// 4. Group consumers by module_prefix and file_path
	std::map<std::pair<std::string, std::string>, std::set<int>> group_lines;
	std::map<std::pair<std::string, std::string>, std::set<std::string>> group_types;

	for (const auto &rc : raw_consumers) {
		auto key = std::make_pair(rc.module_prefix, rc.file_path);
		if (rc.line > 0) {
			group_lines[key].insert(rc.line);
		}
		if (!rc.cell_type.empty()) {
			group_types[key].insert(rc.cell_type);
		}
	}

	for (const auto &[key, lines] : group_lines) {
		consumer_group cg;
		cg.module_prefix = key.first;
		cg.file_path = key.second;
		cg.lines.assign(lines.begin(), lines.end());

		// Build description
		const auto &types = group_types[key];
		std::string type_summary;
		for (const auto &t : types) {
			if (!type_summary.empty()) type_summary += ", ";
			type_summary += t;
		}
		cg.description = type_summary;

		trace.consumers.push_back(std::move(cg));
	}

	return trace;
}

std::string yosys_netlist_reader::format_trace_summary(const signal_source_trace &trace)
{
	std::ostringstream ss;
	ss << std::format("### Signal Source Trace: `{}`\n", trace.query_signal);
	if (trace.matched_net_name != trace.query_signal) {
		ss << std::format("- **Matched Net**: `{}`\n", trace.matched_net_name);
	}
	ss << std::format("- **Width**: {}-bit (Bit IDs: {})\n\n",
	                  trace.bit_width,
	                  trace.bit_ids.size() <= 4
	                      ? std::format("{}", trace.bit_ids.front())
	                      : std::format("{}..{}", trace.bit_ids.front(), trace.bit_ids.back()));

	// 1. Hierarchy Declarations
	ss << "#### Hierarchy Declarations & Port Connections\n";
	if (trace.hierarchy_declarations.empty()) {
		ss << "*None recorded*\n";
	} else {
		for (const auto &decl : trace.hierarchy_declarations) {
			ss << std::format("- `{}` [{}]: {}\n",
			                  decl.hierarchical_name,
			                  decl.src_location.empty() ? "N/A" : decl.src_location,
			                  decl.role);
		}
	}
	ss << "\n";

	// 2. Drivers
	ss << "#### Drivers\n";
	if (trace.is_top_port && trace.top_port_direction == "input") {
		ss << std::format("- Top-level primary input port (`{}`)\n", trace.matched_net_name);
	}
	if (trace.drivers.empty()) {
		if (!trace.is_top_port) {
			ss << "*No cell drivers detected (undriven or constant wire)*\n";
		}
	} else {
		for (const auto &d : trace.drivers) {
			ss << std::format("- `{}` [{}]: {}\n",
			                  extract_module_prefix(d.cell_name),
			                  d.src_location.empty() ? "N/A" : d.src_location,
			                  d.description);
		}
	}
	ss << "\n";

	// 3. Consumers
	ss << "#### Consumers (Sinks)\n";
	if (trace.is_top_port && trace.top_port_direction == "output") {
		ss << std::format("- Top-level primary output port (`{}`)\n", trace.matched_net_name);
	}
	if (trace.consumers.empty()) {
		if (!trace.is_top_port) {
			ss << "*No downstream cell consumers detected (unconnected / sinkless)*\n";
		}
	} else {
		for (const auto &c : trace.consumers) {
			std::string lines_str;
			for (size_t i = 0; i < c.lines.size(); ++i) {
				if (i > 0) lines_str += ", ";
				lines_str += std::to_string(c.lines[i]);
			}
			ss << std::format("- `{}` [{}:{}]: {}\n",
			                  c.module_prefix,
			                  c.file_path,
			                  lines_str.empty() ? "?" : lines_str,
			                  c.description);
		}
	}

	ss << "\n*Note: Reports structural net connectivity (driving and consuming cells). "
	      "For procedural gating conditions (e.g. enable predicates or if/case mux branches), inspect the driver source lines above.*\n";

	return ss.str();
}

bool yosys_netlist_reader::is_flat_gate_level() const
{
	if (netnames_.empty()) return false;
	size_t dotted_count = 0;
	for (const auto &[name, _] : netnames_) {
		if (name.find('.') != std::string::npos) {
			++dotted_count;
		}
	}
	return dotted_count == 0;
}

} // namespace verilog
