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

	size_t bram_pos = s.find(".0.");
	if (bram_pos != std::string::npos && bram_pos > 0 && bram_pos + 3 < s.size() &&
	    std::isdigit(static_cast<unsigned char>(s[bram_pos + 3]))) {
		if (best_pos == std::string::npos || bram_pos < best_pos) {
			best_pos = bram_pos;
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

namespace {

struct always_ff_info {
	std::string file_part;
	std::vector<std::string> lhs_list;
	std::set<std::string> lhs_set;
	std::map<std::string, std::set<std::string>> lhs_to_rhs;
	std::map<std::string, bool> lhs_has_op;
};

std::string_view extract_first_nonlib_seg(std::string_view raw_src)
{
	size_t start = 0;
	while (start < raw_src.size()) {
		size_t bar = raw_src.find('|', start);
		std::string_view seg = (bar == std::string_view::npos) ? raw_src.substr(start) : raw_src.substr(start, bar - start);
		if (!seg.empty() && !yosys_netlist_reader::is_library_source(seg) && !seg.ends_with(":0.0-0.0")) {
			return seg;
		}
		if (bar == std::string_view::npos) break;
		start = bar + 1;
	}
	return {};
}

std::string extract_src_file_from_raw(std::string_view raw_src)
{
	if (raw_src.empty()) {
		return "";
	}
	std::string_view seg = extract_first_nonlib_seg(raw_src);
	if (seg.empty()) {
		size_t bar = raw_src.find('|');
		seg = (bar != std::string_view::npos) ? raw_src.substr(0, bar) : raw_src;
	}
	size_t colon = seg.rfind(':');
	if (colon != std::string_view::npos) {
		return std::string(seg.substr(0, colon));
	}
	return std::string(seg);
}

bool is_verilog_constant_rhs(std::string_view rhs)
{
	while (!rhs.empty() && (rhs.front() == ' ' || rhs.front() == '\t')) rhs.remove_prefix(1);
	while (!rhs.empty() && (rhs.back() == ' ' || rhs.back() == '\t')) rhs.remove_suffix(1);
	if (rhs.empty()) return true;
	if (std::isdigit(static_cast<unsigned char>(rhs.front())) || rhs.front() == '\'') {
		for (size_t i = 0; i < rhs.size(); ++i) {
			char c = rhs[i];
			if (c == '+' || c == '-' || c == '&' || c == '|' || c == '^' || c == '?' || c == '(' || c == '{') {
				return false;
			}
		}
		return true;
	}
	return false;
}

always_ff_info parse_always_ff_block(std::string_view clean_seg, std::string_view base_dir)
{
	always_ff_info info;
	if (clean_seg.empty()) return info;

	size_t colon = clean_seg.rfind(':');
	if (colon == std::string_view::npos || colon == 0 || colon + 1 >= clean_seg.size()) {
		return info;
	}

	info.file_part = std::string(clean_seg.substr(0, colon));
	std::string_view range_part = clean_seg.substr(colon + 1);
	size_t dash = range_part.find('-');
	auto parse_line = [](std::string_view tok) -> int {
		size_t dot = tok.find('.');
		std::string s(dot != std::string_view::npos ? tok.substr(0, dot) : tok);
		try { return std::stoi(s); } catch (...) { return 0; }
	};
	int sline = parse_line(dash != std::string_view::npos ? range_part.substr(0, dash) : range_part);
	int eline = (dash != std::string_view::npos) ? parse_line(range_part.substr(dash + 1)) : sline;
	if (sline <= 0 || eline < sline) {
		return info;
	}

	std::filesystem::path resolved_path;
	std::vector<std::filesystem::path> search_attempts = { std::filesystem::path(info.file_part) };
	if (!base_dir.empty()) {
		std::filesystem::path bp(base_dir);
		search_attempts.push_back(bp / info.file_part);
		if (bp.has_parent_path()) {
			search_attempts.push_back(bp.parent_path() / info.file_part);
			if (bp.parent_path().has_parent_path()) {
				search_attempts.push_back(bp.parent_path().parent_path() / info.file_part);
			}
		}
	}
	for (const auto &cand_p : search_attempts) {
		std::error_code ec;
		if (std::filesystem::exists(cand_p, ec) && std::filesystem::is_regular_file(cand_p, ec)) {
			resolved_path = cand_p;
			break;
		}
	}
	if (resolved_path.empty()) {
		return info;
	}

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
					std::string lhs_ident = line.substr(p, id_end - p);
					if (!lhs_ident.empty() && !std::isdigit(static_cast<unsigned char>(lhs_ident[0]))) {
						if (!info.lhs_set.contains(lhs_ident)) {
							info.lhs_set.insert(lhs_ident);
							info.lhs_list.push_back(lhs_ident);
						}

						// Parse RHS expression up to ';'
						size_t rhs_start = pos + 2;
						size_t semi = line.find(';', rhs_start);
						std::string_view rhs = (semi != std::string::npos)
							? std::string_view(line).substr(rhs_start, semi - rhs_start)
							: std::string_view(line).substr(rhs_start);

						if (!is_verilog_constant_rhs(rhs)) {
							for (char ch : rhs) {
								if (ch == '+' || ch == '-' || ch == '&' || ch == '|' || ch == '^' || ch == '~' || ch == '?' || ch == '{') {
									info.lhs_has_op[lhs_ident] = true;
								}
							}
							size_t ri = 0;
							while (ri < rhs.size()) {
								if (rhs[ri] == '\'') {
									ri++;
									while (ri < rhs.size() && (std::isalnum(static_cast<unsigned char>(rhs[ri])) || rhs[ri] == '_')) {
										ri++;
									}
									continue;
								}
								if (std::isalpha(static_cast<unsigned char>(rhs[ri])) || rhs[ri] == '_') {
									size_t r_start = ri;
									while (ri < rhs.size() && (std::isalnum(static_cast<unsigned char>(rhs[ri])) || rhs[ri] == '_')) {
										ri++;
									}
									// Skip indexing [ ... ] directly attached to RHS identifier
									std::string rhs_id(rhs.substr(r_start, ri - r_start));
									if (rhs_id != lhs_ident && rhs_id != "begin" && rhs_id != "end" && rhs_id != "if" && rhs_id != "else") {
										info.lhs_to_rhs[lhs_ident].insert(std::move(rhs_id));
									}
								} else {
									ri++;
								}
							}
						}
					}
				}
			}
			pos += 2;
		}
	}
	return info;
}

} // namespace

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
	size_t lb = leaf.rfind('[');
	std::string leaf_bit_sfx = (lb != std::string::npos && leaf.ends_with(']')) ? leaf.substr(lb) : "";
	if (!leaf_bit_sfx.empty()) {
		leaf.resize(lb);
	}

	std::string_view clean_seg = extract_first_nonlib_seg(raw_src);
	always_ff_info ff = parse_always_ff_block(clean_seg, base_dir);

	if (ff.lhs_set.size() == 1) {
		const std::string &reg = *ff.lhs_set.begin();
		return scope.empty() ? (reg + leaf_bit_sfx) : std::format("{}.{}{}", scope, reg, leaf_bit_sfx);
	}

	if (leaf.starts_with("next_") && leaf.size() > 5) {
		std::string cand_reg = leaf.substr(5);
		size_t sb_pos = cell_str.find("_SB_");
		bool direct_dff = (sb_pos != std::string::npos && std::string_view(cell_str).substr(sb_pos).starts_with("_SB_DFF"));
		if (ff.lhs_set.contains(cand_reg) || (ff.lhs_set.empty() && direct_dff)) {
			return scope.empty() ? (cand_reg + leaf_bit_sfx) : std::format("{}.{}{}", scope, cand_reg, leaf_bit_sfx);
		}
	}

	if (!leaf.empty() && ff.lhs_set.contains(leaf)) {
		return scope.empty() ? (leaf + leaf_bit_sfx) : std::format("{}.{}{}", scope, leaf, leaf_bit_sfx);
	}

	// Check if leaf matches the RHS identifier of a single LHS register in the always_ff block (e.g. input2_reg <= input2)
	if (!leaf.empty() && !ff.lhs_to_rhs.empty()) {
		std::string matched_lhs;
		size_t match_count = 0;
		for (const auto &[lhs, rset] : ff.lhs_to_rhs) {
			if (rset.contains(leaf)) {
				matched_lhs = lhs;
				match_count++;
			}
		}
		if (match_count == 1) {
			return scope.empty() ? (matched_lhs + leaf_bit_sfx) : std::format("{}.{}{}", scope, matched_lhs, leaf_bit_sfx);
		}
	}

	return "";
}

yosys_netlist_reader::netlist_correlation_tables yosys_netlist_reader::build_correlation_tables(const nlohmann::json &mod_obj, std::string_view base_dir)
{
	netlist_correlation_tables tables;
	if (!mod_obj.is_object()) {
		return tables;
	}

	struct raw_cell_conn {
		std::string type;
		std::string src_raw;
		std::string src_file;
		bool is_dff{false};
		bool is_lut{false};
		std::vector<int> q_bits;
		std::vector<int> d_bits;
		std::vector<int> e_bits;
		std::vector<int> out_bits;
		std::vector<int> in_bits;
	};

	std::map<std::string, raw_cell_conn> cells_map;
	std::map<int, std::string> bit_driver_file;
	std::map<int, std::string> bit_driver_cell;
	std::map<int, size_t> bit_sink_counts;
	std::map<int, std::string> bit_to_dff_d;
	std::map<int, std::string> bit_to_dff_ctrl;
	std::map<std::string, int> lut_o_bit;

	if (mod_obj.contains("cells") && mod_obj["cells"].is_object()) {
		for (const auto &[cname, cdata] : mod_obj["cells"].items()) {
			if (!cdata.is_object()) continue;
			raw_cell_conn rc;
			rc.type = cdata.value("type", "");
			if (cdata.contains("attributes") && cdata["attributes"].is_object()) {
				rc.src_raw = cdata["attributes"].value("src", "");
			}
			rc.src_file = extract_src_file_from_raw(rc.src_raw);
			bool valid_c_file = !rc.src_file.empty() && !is_library_source(rc.src_file);

			rc.is_dff = (rc.type.find("DFF") != std::string::npos ||
			             rc.type.starts_with("$dff") || rc.type.starts_with("$adff") ||
			             rc.type.starts_with("$sdff") || rc.type.starts_with("$dffe") ||
			             rc.type.starts_with("$adffe") || rc.type.starts_with("$sdffe") ||
			             rc.type.starts_with("$sdffce") || rc.type.starts_with("$_DFF") ||
			             rc.type.starts_with("$_SDFF") || rc.type.starts_with("$_DFFE") ||
			             rc.type.starts_with("FDRE") || rc.type.starts_with("FDSE") ||
			             rc.type.starts_with("FDCE") || rc.type.starts_with("FDPE"));
			rc.is_lut = (rc.type.find("LUT") != std::string::npos || rc.type == "$lut");

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
					if (!conn_val.is_array()) continue;
					bool is_input = false;
					auto it_dir = pdirs.find(pname);
					if (it_dir != pdirs.end()) {
						is_input = (it_dir->second == "input");
					} else {
						is_input = (pname != "Y" && pname != "y" && pname != "Q" && pname != "q" &&
						            pname != "O" && pname != "CO" && pname != "COUT" && pname != "RDATA");
					}

					for (const auto &b_val : conn_val) {
						if (!b_val.is_number_integer()) continue;
						int b = b_val.get<int>();
						if (b <= 0) continue;

						if (is_input) {
							bit_sink_counts[b]++;
							rc.in_bits.push_back(b);
							if (rc.is_dff) {
								if (pname == "D" || pname == "d" || pname == "D_IN") {
									rc.d_bits.push_back(b);
									bit_to_dff_d[b] = cname;
								} else if (pname == "E" || pname == "EN" || pname == "CE" || pname == "ENABLE") {
									rc.e_bits.push_back(b);
									bit_to_dff_ctrl[b] = cname;
								} else if (pname == "R" || pname == "S" || pname == "SR" || pname == "RESET" || pname == "SET") {
									bit_to_dff_ctrl[b] = cname;
								}
							}
						} else {
							rc.out_bits.push_back(b);
							bit_driver_cell[b] = cname;
							if (valid_c_file) {
								bit_driver_file[b] = rc.src_file;
							}
							if (rc.is_dff && (pname == "Q" || pname == "q")) {
								rc.q_bits.push_back(b);
							} else if (rc.is_lut && (pname == "O" || pname == "Y")) {
								lut_o_bit[cname] = b;
							}
						}
					}
				}
			}
			cells_map[cname] = std::move(rc);
		}
	}

	struct user_net_cand {
		std::string name;
		std::string scope;
		std::string leaf;
		std::string src_file;
		std::string src_raw;
		std::vector<int> bits;
	};
	struct bit_user_cand {
		std::string name;
		std::string fmt;
		std::string scope;
		std::string leaf;
		std::string src_file;
		std::string src_raw;
		size_t idx{0};
		size_t width{1};
	};

	std::vector<user_net_cand> user_nets;
	std::map<std::vector<int>, std::vector<size_t>> by_bits;
	std::map<std::string, std::vector<int>> user_net_bits;
	std::map<int, std::vector<bit_user_cand>> bit_to_user_cands;

	if (mod_obj.contains("netnames") && mod_obj["netnames"].is_object()) {
		for (const auto &[nname, ndata] : mod_obj["netnames"].items()) {
			if (!ndata.is_object()) continue;
			int hide_name = ndata.value("hide_name", 0);
			if (hide_name != 0 || nname.starts_with("$") || nname.starts_with("new_n")) continue;

			bool is_synth = (nname.find("_SB_") != std::string::npos ||
			                 nname.find("$SB_") != std::string::npos ||
			                 nname.find("$sb_") != std::string::npos ||
			                 nname.find("$func$") != std::string::npos);
			if (is_synth) continue;

			std::string src_raw;
			if (ndata.contains("attributes") && ndata["attributes"].is_object()) {
				src_raw = ndata["attributes"].value("src", "");
			}
			std::string src_file = extract_src_file_from_raw(src_raw);
			if (is_library_source(src_file)) continue;

			std::vector<int> bits;
			if (ndata.contains("bits") && ndata["bits"].is_array()) {
				for (const auto &b_val : ndata["bits"]) {
					bits.push_back(b_val.is_number_integer() ? b_val.get<int>() : -1);
				}
			}
			bool has_valid_bit = false;
			for (int b : bits) {
				if (b > 0) {
					has_valid_bit = true;
					break;
				}
			}
			if (!has_valid_bit) continue;

			size_t dot = nname.rfind('.');
			std::string scope = (dot != std::string::npos) ? nname.substr(0, dot) : "";
			std::string leaf = (dot != std::string::npos) ? nname.substr(dot + 1) : nname;
			if (!src_file.empty()) {
				tables.file_to_scopes[src_file].insert(scope);
			}

			user_net_bits[nname] = bits;
			size_t u_idx = user_nets.size();
			user_nets.push_back({nname, scope, leaf, src_file, src_raw, bits});
			by_bits[bits].push_back(u_idx);

			for (size_t i = 0; i < bits.size(); ++i) {
				int b = bits[i];
				if (b > 0) {
					std::string fmt = (bits.size() > 1) ? std::format("{}[{}]", nname, i) : nname;
					bit_to_user_cands[b].push_back({nname, std::move(fmt), scope, leaf, src_file, src_raw, i, bits.size()});
				}
			}
		}
	}

	// Score equivalence classes of identical bit vectors to build net_alias_map
	for (const auto &[bvec, indices] : by_bits) {
		std::set<std::string> drv_files;
		for (int b : bvec) {
			auto it = bit_driver_file.find(b);
			if (it != bit_driver_file.end() && !it->second.empty()) {
				drv_files.insert(it->second);
			}
		}
		std::map<std::string, size_t> src_raw_counts;
		std::set<std::string> drv_leaf_names;
		for (size_t idx : indices) {
			const auto &un = user_nets[idx];
			if (!un.src_raw.empty()) {
				src_raw_counts[un.src_raw]++;
			}
			if (!un.src_file.empty() && drv_files.contains(un.src_file)) {
				drv_leaf_names.insert(un.leaf);
			}
		}

		auto score_cand = [&](const user_net_cand &un) {
			bool matches_drv = (!un.src_file.empty() && drv_files.contains(un.src_file)) || drv_leaf_names.contains(un.leaf);
			int drv_rank = matches_drv ? 0 : 1;
			// Demote replicated submodule input ports (e.g. A_reg.load_value .. L_reg.load_value sharing identical src_raw)
			int rep_rank = (!un.src_raw.empty() && src_raw_counts[un.src_raw] > 1) ? 1 : 0;
			size_t dots = static_cast<size_t>(std::count(un.name.begin(), un.name.end(), '.'));
			return std::make_tuple(drv_rank, rep_rank, dots, un.name.size(), un.name);
		};

		size_t best_idx = indices.front();
		for (size_t idx : indices) {
			if (score_cand(user_nets[idx]) < score_cand(user_nets[best_idx])) {
				best_idx = idx;
			}
		}
		const std::string &best_name = user_nets[best_idx].name;
		if (indices.size() > 1) {
			for (size_t idx : indices) {
				if (user_nets[idx].name != best_name) {
					tables.net_alias_map[user_nets[idx].name] = best_name;
				}
			}
		}
	}

	// Build per-bit canonical user net map (bit_to_user_net)
	for (const auto &[b, cands] : bit_to_user_cands) {
		std::string drv_f = bit_driver_file.contains(b) ? bit_driver_file[b] : "";
		std::map<std::string, size_t> src_raw_counts;
		for (const auto &c : cands) {
			if (!c.src_raw.empty()) src_raw_counts[c.src_raw]++;
		}
		auto score_bit_cand = [&](const bit_user_cand &c) {
			int drv_rank = (!drv_f.empty() && c.src_file == drv_f) ? 0 : 1;
			int rep_rank = (!c.src_raw.empty() && src_raw_counts[c.src_raw] > 1) ? 1 : 0;
			size_t dots = static_cast<size_t>(std::count(c.name.begin(), c.name.end(), '.'));
			return std::make_tuple(drv_rank, rep_rank, dots, c.name.size(), c.fmt);
		};
		auto best_it = std::min_element(cands.begin(), cands.end(), [&](const auto &a, const auto &b_cand) {
			return score_bit_cand(a) < score_bit_cand(b_cand);
		});
		std::string canon_base = tables.net_alias_map.contains(best_it->name) ? tables.net_alias_map[best_it->name] : best_it->name;
		tables.bit_to_user_net[b] = (best_it->width > 1) ? std::format("{}[{}]", canon_base, best_it->idx) : canon_base;
	}

	// Populate lut_to_packed_dff and cell_to_output_user_net
	for (const auto &[lut_name, o_bit] : lut_o_bit) {
		if (o_bit > 0) {
			if (bit_to_dff_d.contains(o_bit)) {
				tables.lut_to_packed_dff[lut_name] = bit_to_dff_d[o_bit];
			} else if (bit_sink_counts[o_bit] == 1 && bit_to_dff_ctrl.contains(o_bit)) {
				tables.lut_to_packed_dff[lut_name] = bit_to_dff_ctrl[o_bit];
			}
		}
	}
	for (const auto &[cname, rc] : cells_map) {
		if (!rc.is_dff) {
			for (int ob : rc.out_bits) {
				if (ob > 0 && tables.bit_to_user_net.contains(ob)) {
					tables.cell_to_output_user_net[cname] = tables.bit_to_user_net[ob];
					break;
				}
			}
		}
	}

	// Resolve DFF register names using Yosys wire connectivity + always_ff analysis
	std::map<std::string, always_ff_info> ff_cache;
	for (const auto &[cname, rc] : cells_map) {
		if (!rc.is_dff) continue;

		std::string_view clean_seg = extract_first_nonlib_seg(rc.src_raw);
		std::string seg_key(clean_seg);
		if (!clean_seg.empty() && !ff_cache.contains(seg_key)) {
			ff_cache[seg_key] = parse_always_ff_block(clean_seg, base_dir);
		}
		const always_ff_info &ff = clean_seg.empty() ? always_ff_info{} : ff_cache[seg_key];

		std::string norm_cell = normalize_net_name(cname);
		size_t cell_dot = norm_cell.rfind('.');
		std::string cell_scope = (cell_dot != std::string::npos) ? norm_cell.substr(0, cell_dot) : "";
		std::string cell_leaf = (cell_dot != std::string::npos) ? norm_cell.substr(cell_dot + 1) : norm_cell;
		size_t cell_lb = cell_leaf.rfind('[');
		std::string cell_bit_sfx = (cell_lb != std::string::npos && cell_leaf.ends_with(']')) ? cell_leaf.substr(cell_lb) : "";
		if (!cell_bit_sfx.empty()) {
			cell_leaf.resize(cell_lb);
		}

		// Step 1: Determine true hierarchical instance scope via wire connectivity
		std::string scope = cell_scope;
		bool scope_verified = true;
		if (!rc.src_file.empty() && tables.file_to_scopes.contains(rc.src_file)) {
			const auto &valid_scopes = tables.file_to_scopes.at(rc.src_file);
			if (valid_scopes.size() == 1) {
				scope = *valid_scopes.begin();
			} else if (valid_scopes.size() > 1) {
				scope.clear();
				scope_verified = false;
				auto find_unique_scope_on_bits = [&](const std::vector<int> &bits_vec) -> std::optional<std::string> {
					std::set<std::string> matched_scopes;
					for (int b : bits_vec) {
						auto it_c = bit_to_user_cands.find(b);
						if (it_c == bit_to_user_cands.end()) continue;
						for (const auto &c : it_c->second) {
							if (c.src_file == rc.src_file && valid_scopes.contains(c.scope)) {
								matched_scopes.insert(c.scope);
							}
						}
					}
					if (matched_scopes.size() == 1) {
						return *matched_scopes.begin();
					}
					return std::nullopt;
				};

				for (const auto *bvec : {&rc.q_bits, &rc.e_bits, &rc.d_bits}) {
					if (auto sc = find_unique_scope_on_bits(*bvec)) {
						scope = *sc;
						scope_verified = true;
						break;
					}
				}
				if (!scope_verified) {
					// Check 1-hop upstream driver inputs of E and D
					for (const auto *bvec : {&rc.e_bits, &rc.d_bits}) {
						std::vector<int> up_bits;
						for (int b : *bvec) {
							auto it_drv = bit_driver_cell.find(b);
							if (it_drv != bit_driver_cell.end() && cells_map.contains(it_drv->second)) {
								const auto &drv_rc = cells_map.at(it_drv->second);
								up_bits.insert(up_bits.end(), drv_rc.in_bits.begin(), drv_rc.in_bits.end());
							}
						}
						if (auto sc = find_unique_scope_on_bits(up_bits)) {
							scope = *sc;
							scope_verified = true;
							break;
						}
					}
				}
				if (!scope_verified && valid_scopes.contains(cell_scope)) {
					scope = cell_scope;
					scope_verified = true;
				}
			}
		}

		int q_bit = rc.q_bits.empty() ? -1 : rc.q_bits.front();
		int d_bit = rc.d_bits.empty() ? -1 : rc.d_bits.front();

		// Step 2: Check direct user net on Q output bit
		if (q_bit > 0 && bit_to_user_cands.contains(q_bit)) {
			const auto &q_cands = bit_to_user_cands[q_bit];
			// Prefer candidate in resolved scope whose leaf is an LHS register in always_ff
			for (const auto &c : q_cands) {
				if (scope_verified && c.scope == scope && ff.lhs_set.contains(c.leaf)) {
					tables.dff_to_reg_name[cname] = c.fmt;
					break;
				}
			}
			if (!tables.dff_to_reg_name.contains(cname)) {
				for (const auto &c : q_cands) {
					if ((scope_verified && c.scope == scope) || (!rc.src_file.empty() && c.src_file == rc.src_file)) {
						tables.dff_to_reg_name[cname] = c.fmt;
						break;
					}
				}
			}
			if (!tables.dff_to_reg_name.contains(cname) &&
			    (rc.src_file.empty() || is_library_source(rc.src_file)) &&
			    tables.bit_to_user_net.contains(q_bit)) {
				tables.dff_to_reg_name[cname] = tables.bit_to_user_net[q_bit];
			}
		}
		if (tables.dff_to_reg_name.contains(cname)) {
			continue;
		}

		// Step 3: Single-register always_ff block
		if (ff.lhs_set.size() == 1 && scope_verified) {
			const std::string &lhs = *ff.lhs_set.begin();
			std::string bit_sfx;
			if (d_bit > 0 && bit_to_user_cands.contains(d_bit)) {
				for (const auto &c : bit_to_user_cands[d_bit]) {
					if ((c.scope == scope || c.src_file == rc.src_file) && c.width > 1) {
						bit_sfx = std::format("[{}]", c.idx);
						break;
					}
				}
			}
			if (bit_sfx.empty()) {
				bit_sfx = cell_bit_sfx;
			}
			tables.dff_to_reg_name[cname] = scope.empty() ? (lhs + bit_sfx) : std::format("{}.{}{}", scope, lhs, bit_sfx);
			continue;
		}

		// Step 4: Multi-register shared always_ff block disambiguation via wire connectivity
		if (ff.lhs_set.size() > 1 && scope_verified) {
			// 4a. Direct D-pin RHS user net match
			if (d_bit > 0 && bit_to_user_cands.contains(d_bit)) {
				std::set<std::string> matched_lhs;
				std::string matched_bit_sfx;
				for (const auto &c : bit_to_user_cands[d_bit]) {
					if (c.scope == scope || c.src_file == rc.src_file) {
						for (const auto &[lhs, rset] : ff.lhs_to_rhs) {
							bool has_op = ff.lhs_has_op.contains(lhs) && ff.lhs_has_op.at(lhs);
							if (rset.contains(c.leaf) && !has_op) {
								matched_lhs.insert(lhs);
								if (c.width > 1) {
									matched_bit_sfx = std::format("[{}]", c.idx);
								}
							}
						}
					}
				}
				if (matched_lhs.size() == 1) {
					const std::string &lhs = *matched_lhs.begin();
					tables.dff_to_reg_name[cname] = scope.empty()
						? (lhs + matched_bit_sfx)
						: std::format("{}.{}{}", scope, lhs, matched_bit_sfx);
					continue;
				}
			}

			// 4b. 1-hop upstream D-driver LUT match (e.g. ip_plus_one <= next_ip + 1)
			if (d_bit > 0 && bit_driver_cell.contains(d_bit)) {
				const std::string &drv_name = bit_driver_cell[d_bit];
				if (cells_map.contains(drv_name)) {
					const auto &drv_rc = cells_map.at(drv_name);
					if (!drv_rc.is_dff) {
						std::set<std::string> op_matched_lhs;
						std::set<std::string> any_matched_lhs;
						std::string matched_bit_sfx;
						for (int ib : drv_rc.in_bits) {
							if (!bit_to_user_cands.contains(ib)) continue;
							for (const auto &c : bit_to_user_cands[ib]) {
								if (c.scope == scope || c.src_file == rc.src_file) {
									for (const auto &[lhs, rset] : ff.lhs_to_rhs) {
										if (rset.contains(c.leaf)) {
											any_matched_lhs.insert(lhs);
											if (ff.lhs_has_op.contains(lhs) && ff.lhs_has_op.at(lhs)) {
												op_matched_lhs.insert(lhs);
												if (c.width > 1 && matched_bit_sfx.empty()) {
													matched_bit_sfx = std::format("[{}]", c.idx);
												}
											}
										}
									}
								}
							}
						}
						if (op_matched_lhs.size() == 1) {
							const std::string &lhs = *op_matched_lhs.begin();
							tables.dff_to_reg_name[cname] = scope.empty()
								? (lhs + matched_bit_sfx)
								: std::format("{}.{}{}", scope, lhs, matched_bit_sfx);
							continue;
						}
						if (any_matched_lhs.size() == 1) {
							const std::string &lhs = *any_matched_lhs.begin();
							tables.dff_to_reg_name[cname] = scope.empty()
								? (lhs + matched_bit_sfx)
								: std::format("{}.{}{}", scope, lhs, matched_bit_sfx);
							continue;
						}
					}
				}
			}

			// 4c. Elimination of LHS registers whose known user nets do not contain q_bit / d_bit
			std::vector<std::string> remaining_lhs;
			for (const auto &lhs : ff.lhs_list) {
				std::string q_full = scope.empty() ? lhs : std::format("{}.{}", scope, lhs);
				auto it_qnet = user_net_bits.find(q_full);
				if (it_qnet != user_net_bits.end()) {
					if (std::find(it_qnet->second.begin(), it_qnet->second.end(), q_bit) == it_qnet->second.end()) {
						continue;
					}
				}

				bool has_op = ff.lhs_has_op.contains(lhs) && ff.lhs_has_op.at(lhs);
				auto it_rhs = ff.lhs_to_rhs.find(lhs);
				if (!has_op && it_rhs != ff.lhs_to_rhs.end() && it_rhs->second.size() == 1) {
					const std::string &rid = *it_rhs->second.begin();
					std::string r_full = scope.empty() ? rid : std::format("{}.{}", scope, rid);
					auto it_rnet = user_net_bits.find(r_full);
					if (it_rnet != user_net_bits.end()) {
						const auto &rbits = it_rnet->second;
						bool shared_with_other_lhs = false;
						for (const auto &[other_lhs, other_rset] : ff.lhs_to_rhs) {
							if (other_lhs == lhs) continue;
							for (const auto &orid : other_rset) {
								std::string or_full = scope.empty() ? orid : std::format("{}.{}", scope, orid);
								auto it_or = user_net_bits.find(or_full);
								if (it_or != user_net_bits.end() && it_or->second == rbits) {
									shared_with_other_lhs = true;
									break;
								}
							}
							if (shared_with_other_lhs) break;
						}
						if (!shared_with_other_lhs &&
						    std::find(rbits.begin(), rbits.end(), d_bit) == rbits.end()) {
							continue;
						}
					}
				}
				remaining_lhs.push_back(lhs);
			}

			if (remaining_lhs.size() == 1) {
				const std::string &lhs = remaining_lhs.front();
				tables.dff_to_reg_name[cname] = scope.empty() ? lhs : std::format("{}.{}", scope, lhs);
				continue;
			}

			// 4d. Check cell leaf name against remaining LHS candidates when cell_scope matches scope
			if (cell_scope == scope) {
				std::string cand_leaf = (cell_leaf.starts_with("next_") && cell_leaf.size() > 5)
					? cell_leaf.substr(5)
					: cell_leaf;
				if (std::find(remaining_lhs.begin(), remaining_lhs.end(), cand_leaf) != remaining_lhs.end()) {
					tables.dff_to_reg_name[cname] = scope.empty()
						? (cand_leaf + cell_bit_sfx)
						: std::format("{}.{}{}", scope, cand_leaf, cell_bit_sfx);
					continue;
				}
			}
		}
	}

	return tables;
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
