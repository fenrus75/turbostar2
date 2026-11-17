#include "plugins/verilog/vcd_axi_protocol_checker.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <deque>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace verilog {

namespace {

constexpr size_t kMaxViolationsPerInterface = 20;

std::string to_lower_ascii(std::string_view sv)
{
	std::string out(sv);
	for (char &c : out) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

bool contains_xz(std::string_view val) noexcept
{
	if (val.starts_with("0x") || val.starts_with("0X") || val.starts_with("0b") || val.starts_with("0B")) {
		val.remove_prefix(2);
	}
	for (char c : val) {
		if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') {
			return true;
		}
	}
	return false;
}

bool is_bit_one(std::string_view val) noexcept
{
	return val == "1" || val == "0x1" || val == "0b1";
}

bool is_bit_zero(std::string_view val) noexcept
{
	return val == "0" || val == "0x0" || val == "0b0";
}

std::optional<uint64_t> parse_vcd_uint(std::string_view val) noexcept
{
	if (val.empty() || contains_xz(val)) {
		return std::nullopt;
	}
	int base = 10;
	if (val.starts_with("0x") || val.starts_with("0X")) {
		val.remove_prefix(2);
		base = 16;
	} else if (val.starts_with("0b") || val.starts_with("0B")) {
		val.remove_prefix(2);
		base = 2;
	} else {
		// sample_wave defaults to hex ('x') without 0x prefix for multi-bit vectors
		base = 16;
	}
	if (val.empty()) {
		return std::nullopt;
	}
	uint64_t out = 0;
	const char *begin = val.data();
	const char *end = begin + val.size();
	auto [ptr, ec] = std::from_chars(begin, end, out, base);
	if (ec != std::errc{} || ptr != end) {
		return std::nullopt;
	}
	return out;
}

struct scope_signal_index {
	// lower_short_name -> pointer to signal
	std::map<std::string, const vcd_signal *> by_lower_short;
};

std::string find_scope_clock(const std::map<std::string, scope_signal_index> &by_scope,
			     std::string_view scope,
			     std::string_view prefix,
			     const vcd_parser &parser)
{
	static constexpr std::string_view kClkNames[] = {"aclk", "clk", "i_clk", "clock"};
	std::string cur_scope(scope);
	while (!cur_scope.empty()) {
		auto sit = by_scope.find(cur_scope);
		if (sit != by_scope.end()) {
			if (!prefix.empty()) {
				std::string prefixed = std::format("{}aclk", prefix);
				auto pit = sit->second.by_lower_short.find(prefixed);
				if (pit != sit->second.by_lower_short.end() && pit->second->bit_width == 1) {
					return pit->second->full_name;
				}
			}
			for (std::string_view cand : kClkNames) {
				auto cit = sit->second.by_lower_short.find(std::string(cand));
				if (cit != sit->second.by_lower_short.end() && cit->second->bit_width == 1) {
					return cit->second->full_name;
				}
			}
		}
		size_t dot = cur_scope.rfind('.');
		if (dot == std::string::npos) {
			break;
		}
		cur_scope.resize(dot);
	}
	if (auto det = parser.detect_clock()) {
		return det->signal_name;
	}
	return "";
}

void find_scope_reset(const std::map<std::string, scope_signal_index> &by_scope,
		      std::string_view scope,
		      std::string_view prefix,
		      std::string &out_reset,
		      bool &out_active_low)
{
	static constexpr std::pair<std::string_view, bool> kRstNames[] = {
		{"aresetn", true},
		{"rst_n", true},
		{"resetn", true},
		{"rstn", true},
		{"i_rst_n", true},
		{"nreset", true},
		{"areset", false},
		{"rst", false},
		{"reset", false},
		{"i_rst", false},
	};
	std::string cur_scope(scope);
	while (!cur_scope.empty()) {
		auto sit = by_scope.find(cur_scope);
		if (sit != by_scope.end()) {
			if (!prefix.empty()) {
				std::string prefixed = std::format("{}aresetn", prefix);
				auto pit = sit->second.by_lower_short.find(prefixed);
				if (pit != sit->second.by_lower_short.end() && pit->second->bit_width == 1) {
					out_reset = pit->second->full_name;
					out_active_low = true;
					return;
				}
			}
			for (const auto &[cand, active_low] : kRstNames) {
				auto cit = sit->second.by_lower_short.find(std::string(cand));
				if (cit != sit->second.by_lower_short.end() && cit->second->bit_width == 1) {
					out_reset = cit->second->full_name;
					out_active_low = active_low;
					return;
				}
			}
		}
		size_t dot = cur_scope.rfind('.');
		if (dot == std::string::npos) {
			break;
		}
		cur_scope.resize(dot);
	}
}

void add_optional_signal(const scope_signal_index &sidx,
			 std::string_view prefix,
			 std::string_view role,
			 std::map<std::string, std::string> &out_sigs)
{
	std::string key = std::format("{}{}", prefix, role);
	auto it = sidx.by_lower_short.find(key);
	if (it != sidx.by_lower_short.end()) {
		out_sigs[std::string(role)] = it->second->full_name;
	}
}

bool add_required_signal(const scope_signal_index &sidx,
			 std::string_view prefix,
			 std::string_view role,
			 bool require_1bit,
			 std::map<std::string, std::string> &out_sigs)
{
	std::string key = std::format("{}{}", prefix, role);
	auto it = sidx.by_lower_short.find(key);
	if (it == sidx.by_lower_short.end()) {
		return false;
	}
	if (require_1bit && it->second->bit_width != 1) {
		return false;
	}
	out_sigs[std::string(role)] = it->second->full_name;
	return true;
}

std::string_view lookup_sample(const vcd_sample_point &pt, const std::map<std::string, std::string> &sigs, std::string_view role)
{
	auto sig_it = sigs.find(std::string(role));
	if (sig_it == sigs.end()) {
		return {};
	}
	auto val_it = pt.signal_values.find(sig_it->second);
	if (val_it == pt.signal_values.end()) {
		return {};
	}
	return val_it->second;
}

void push_violation(protocol_interface_summary &summary,
		    const protocol_interface &iface,
		    std::string_view rule_id,
		    size_t cycle,
		    uint64_t timestamp,
		    std::string description,
		    std::vector<std::string> sigs)
{
	if (summary.violations.size() >= kMaxViolationsPerInterface) {
		return;
	}
	summary.violations.push_back(protocol_violation{
		iface.protocol,
		iface.interface_name,
		std::string(rule_id),
		cycle,
		timestamp,
		std::move(description),
		std::move(sigs),
	});
}

void check_handshake_channel(const protocol_interface &iface,
			     const vcd_sample_point *prev_pt,
			     const vcd_sample_point &cur_pt,
			     std::string_view ch_label,
			     std::string_view valid_role,
			     std::string_view ready_role,
			     std::span<const std::string_view> payload_roles,
			     protocol_interface_summary &summary)
{
	std::string_view cur_valid = lookup_sample(cur_pt, iface.signals, valid_role);
	std::string_view cur_ready = lookup_sample(cur_pt, iface.signals, ready_role);
	const std::string &valid_sig = iface.signals.at(std::string(valid_role));
	const std::string &ready_sig = iface.signals.at(std::string(ready_role));

	if (contains_xz(cur_valid)) {
		push_violation(summary, iface, "AXI_X_ON_VALID", cur_pt.tick, cur_pt.timestamp,
			       std::format("{} `{}` is `{}` outside reset", ch_label, valid_sig, cur_valid),
			       {valid_sig});
	}

	if (is_bit_one(cur_valid)) {
		for (std::string_view prole : payload_roles) {
			std::string_view pval = lookup_sample(cur_pt, iface.signals, prole);
			if (!pval.empty() && contains_xz(pval)) {
				const std::string &psig = iface.signals.at(std::string(prole));
				push_violation(summary, iface, "AXI_X_ON_PAYLOAD", cur_pt.tick, cur_pt.timestamp,
					       std::format("{} `{}` is `{}` while `{}` is asserted", ch_label, psig, pval, valid_sig),
					       {valid_sig, psig});
				break;
			}
		}
	}

	if (prev_pt != nullptr) {
		std::string_view prev_valid = lookup_sample(*prev_pt, iface.signals, valid_role);
		std::string_view prev_ready = lookup_sample(*prev_pt, iface.signals, ready_role);

		if (is_bit_one(prev_valid) && is_bit_zero(prev_ready)) {
			summary.stall_cycles++;
			if (!is_bit_one(cur_valid)) {
				push_violation(summary, iface, "AXI_VALID_DROPPED", cur_pt.tick, cur_pt.timestamp,
					       std::format("{} `{}` deasserted before `{}` handshake completed",
							   ch_label, valid_sig, ready_sig),
					       {valid_sig, ready_sig});
			} else {
				for (std::string_view prole : payload_roles) {
					std::string_view prev_p = lookup_sample(*prev_pt, iface.signals, prole);
					std::string_view cur_p = lookup_sample(cur_pt, iface.signals, prole);
					if (!prev_p.empty() && !cur_p.empty() && prev_p != cur_p) {
						const std::string &psig = iface.signals.at(std::string(prole));
						push_violation(summary, iface, "AXI_PAYLOAD_CHANGED", cur_pt.tick, cur_pt.timestamp,
							       std::format("{} `{}` changed (`{}` -> `{}`) while `{}` was stalled",
									   ch_label, psig, prev_p, cur_p, valid_sig),
							       {valid_sig, ready_sig, psig});
						break;
					}
				}
			}
		}
	}

	if (is_bit_one(cur_valid) && is_bit_one(cur_ready)) {
		summary.transactions++;
	}
}

} // namespace

vcd_axi_protocol_checker::vcd_axi_protocol_checker(axi_variant variant) noexcept
	: variant_(variant)
{
}

std::string_view vcd_axi_protocol_checker::name() const noexcept
{
	switch (variant_) {
	case axi_variant::axi_stream:
		return "axi_stream";
	case axi_variant::axi_lite:
		return "axi_lite";
	case axi_variant::axi4:
		return "axi4";
	}
	return "axi";
}

std::vector<protocol_interface> vcd_axi_protocol_checker::discover_interfaces(const vcd_parser &parser) const
{
	std::vector<protocol_interface> result;
	std::map<std::string, scope_signal_index> by_scope;

	for (const auto &sig : parser.get_signals()) {
		by_scope[sig.scope].by_lower_short.emplace(to_lower_ascii(sig.short_name), &sig);
	}

	for (const auto &[scope, sidx] : by_scope) {
		if (variant_ == axi_variant::axi_stream) {
			for (const auto &[lower_name, sig_ptr] : sidx.by_lower_short) {
				if (!lower_name.ends_with("tvalid") || sig_ptr->bit_width != 1) {
					continue;
				}
				std::string_view prefix = std::string_view(lower_name).substr(0, lower_name.size() - 6);
				std::map<std::string, std::string> sigs;
				if (!add_required_signal(sidx, prefix, "tvalid", true, sigs) ||
				    !add_required_signal(sidx, prefix, "tready", true, sigs) ||
				    !add_required_signal(sidx, prefix, "tdata", false, sigs)) {
					continue;
				}
				for (std::string_view opt : {"tlast", "tkeep", "tstrb", "tid", "tdest", "tuser"}) {
					add_optional_signal(sidx, prefix, opt, sigs);
				}

				std::string clk = find_scope_clock(by_scope, scope, prefix, parser);
				if (clk.empty()) {
					continue;
				}
				std::string rst;
				bool rst_n = true;
				find_scope_reset(by_scope, scope, prefix, rst, rst_n);

				std::string if_name = prefix.empty() ? scope : std::format("{}.{}*", scope, prefix);
				result.push_back(protocol_interface{
					std::string(name()), std::move(if_name), scope, std::string(prefix), std::move(clk),
					std::move(rst), rst_n, std::move(sigs),
				});
			}
			continue;
		}

		// AXI4-Lite and AXI4 both anchor on *awvalid
		for (const auto &[lower_name, sig_ptr] : sidx.by_lower_short) {
			if (!lower_name.ends_with("awvalid") || sig_ptr->bit_width != 1) {
				continue;
			}
			std::string_view prefix = std::string_view(lower_name).substr(0, lower_name.size() - 7);
			bool has_len = sidx.by_lower_short.contains(std::format("{}awlen", prefix)) ||
				       sidx.by_lower_short.contains(std::format("{}arlen", prefix));
			if (variant_ == axi_variant::axi_lite && has_len) {
				continue;
			}
			if (variant_ == axi_variant::axi4 && !has_len) {
				continue;
			}

			std::map<std::string, std::string> sigs;
			bool ok = add_required_signal(sidx, prefix, "awvalid", true, sigs) &&
				  add_required_signal(sidx, prefix, "awready", true, sigs) &&
				  add_required_signal(sidx, prefix, "awaddr", false, sigs) &&
				  add_required_signal(sidx, prefix, "wvalid", true, sigs) &&
				  add_required_signal(sidx, prefix, "wready", true, sigs) &&
				  add_required_signal(sidx, prefix, "wdata", false, sigs) &&
				  add_required_signal(sidx, prefix, "bvalid", true, sigs) &&
				  add_required_signal(sidx, prefix, "bready", true, sigs) &&
				  add_required_signal(sidx, prefix, "bresp", false, sigs) &&
				  add_required_signal(sidx, prefix, "arvalid", true, sigs) &&
				  add_required_signal(sidx, prefix, "arready", true, sigs) &&
				  add_required_signal(sidx, prefix, "araddr", false, sigs) &&
				  add_required_signal(sidx, prefix, "rvalid", true, sigs) &&
				  add_required_signal(sidx, prefix, "rready", true, sigs) &&
				  add_required_signal(sidx, prefix, "rdata", false, sigs) &&
				  add_required_signal(sidx, prefix, "rresp", false, sigs);

			if (ok && variant_ == axi_variant::axi4) {
				ok = add_required_signal(sidx, prefix, "awlen", false, sigs) &&
				     add_required_signal(sidx, prefix, "arlen", false, sigs) &&
				     add_required_signal(sidx, prefix, "wlast", true, sigs) &&
				     add_required_signal(sidx, prefix, "rlast", true, sigs);
			}
			if (!ok) {
				continue;
			}

			for (std::string_view opt : {"wstrb", "awid", "bid", "arid", "rid", "awsize", "arsize", "awburst", "arburst"}) {
				add_optional_signal(sidx, prefix, opt, sigs);
			}

			std::string clk = find_scope_clock(by_scope, scope, prefix, parser);
			if (clk.empty()) {
				continue;
			}
			std::string rst;
			bool rst_n = true;
			find_scope_reset(by_scope, scope, prefix, rst, rst_n);

			std::string if_name = prefix.empty() ? scope : std::format("{}.{}*", scope, prefix);
			result.push_back(protocol_interface{
				std::string(name()), std::move(if_name), scope, std::string(prefix), std::move(clk),
				std::move(rst), rst_n, std::move(sigs),
			});
		}
	}

	return result;
}

protocol_interface_summary vcd_axi_protocol_checker::check(const vcd_parser &parser, const protocol_interface &iface) const
{
	protocol_interface_summary summary;
	summary.iface = iface;

	std::vector<std::string> req_sigs;
	std::set<std::string> unique_sigs;
	for (const auto &[role, full_name] : iface.signals) {
		if (unique_sigs.insert(full_name).second) {
			req_sigs.push_back(full_name);
		}
	}
	if (!iface.reset_signal.empty() && unique_sigs.insert(iface.reset_signal).second) {
		req_sigs.push_back(iface.reset_signal);
	}

	std::string sample_err;
	auto samples = parser.sample_wave(iface.clock_signal, "posedge", req_sigs, std::nullopt, std::nullopt, std::nullopt,
					  std::nullopt, 0, sample_err, false, 'x');
	if (!sample_err.empty() || samples.empty()) {
		return summary;
	}

	auto is_in_reset = [&](const vcd_sample_point &pt) -> bool {
		if (iface.reset_signal.empty()) {
			return false;
		}
		auto it = pt.signal_values.find(iface.reset_signal);
		if (it == pt.signal_values.end()) {
			return false;
		}
		return iface.reset_active_low ? is_bit_zero(it->second) : is_bit_one(it->second);
	};

	const vcd_sample_point *prev_active_pt = nullptr;
	bool skip_first_post_reset = false;

	// AXI4-Lite / AXI4 transaction tracking
	size_t pending_aw = 0;
	size_t pending_w_last = 0;
	size_t pending_ar = 0;
	std::deque<uint64_t> expected_w_burst_lens;
	std::deque<uint64_t> expected_r_burst_lens;
	uint64_t cur_w_beats = 0;
	uint64_t cur_r_beats = 0;
	bool reported_orphan_b = false;
	bool reported_orphan_r = false;

	for (const auto &pt : samples) {
		if (is_in_reset(pt)) {
			prev_active_pt = nullptr;
			skip_first_post_reset = true;
			pending_aw = 0;
			pending_w_last = 0;
			pending_ar = 0;
			expected_w_burst_lens.clear();
			expected_r_burst_lens.clear();
			cur_w_beats = 0;
			cur_r_beats = 0;
			reported_orphan_b = false;
			reported_orphan_r = false;
			continue;
		}
		if (skip_first_post_reset) {
			skip_first_post_reset = false;
			prev_active_pt = &pt;
			continue;
		}

		summary.cycles_checked++;

		if (variant_ == axi_variant::axi_stream) {
			static constexpr std::string_view kAxisPayloads[] = {"tdata", "tlast", "tkeep", "tstrb", "tid", "tdest", "tuser"};
			check_handshake_channel(iface, prev_active_pt, pt, "AXIS", "tvalid", "tready", kAxisPayloads, summary);
			prev_active_pt = &pt;
			continue;
		}

		static constexpr std::string_view kAwPayloads[] = {"awaddr", "awlen", "awid", "awsize", "awburst"};
		static constexpr std::string_view kWPayloads[] = {"wdata", "wstrb", "wlast"};
		static constexpr std::string_view kBPayloads[] = {"bresp", "bid"};
		static constexpr std::string_view kArPayloads[] = {"araddr", "arlen", "arid", "arsize", "arburst"};
		static constexpr std::string_view kRPayloads[] = {"rdata", "rresp", "rlast", "rid"};

		check_handshake_channel(iface, prev_active_pt, pt, "AW", "awvalid", "awready", kAwPayloads, summary);
		check_handshake_channel(iface, prev_active_pt, pt, "W", "wvalid", "wready", kWPayloads, summary);
		check_handshake_channel(iface, prev_active_pt, pt, "B", "bvalid", "bready", kBPayloads, summary);
		check_handshake_channel(iface, prev_active_pt, pt, "AR", "arvalid", "arready", kArPayloads, summary);
		check_handshake_channel(iface, prev_active_pt, pt, "R", "rvalid", "rready", kRPayloads, summary);

		bool aw_hs = is_bit_one(lookup_sample(pt, iface.signals, "awvalid")) &&
			     is_bit_one(lookup_sample(pt, iface.signals, "awready"));
		bool w_hs = is_bit_one(lookup_sample(pt, iface.signals, "wvalid")) &&
			    is_bit_one(lookup_sample(pt, iface.signals, "wready"));
		bool b_valid = is_bit_one(lookup_sample(pt, iface.signals, "bvalid"));
		bool b_hs = b_valid && is_bit_one(lookup_sample(pt, iface.signals, "bready"));

		bool ar_hs = is_bit_one(lookup_sample(pt, iface.signals, "arvalid")) &&
			     is_bit_one(lookup_sample(pt, iface.signals, "arready"));
		bool r_valid = is_bit_one(lookup_sample(pt, iface.signals, "rvalid"));
		bool r_hs = r_valid && is_bit_one(lookup_sample(pt, iface.signals, "rready"));

		if (aw_hs) {
			pending_aw++;
			if (variant_ == axi_variant::axi4) {
				auto len = parse_vcd_uint(lookup_sample(pt, iface.signals, "awlen")).value_or(0);
				expected_w_burst_lens.push_back(len + 1);
			}
		}

		if (w_hs) {
			if (variant_ == axi_variant::axi_lite) {
				pending_w_last++;
			} else {
				cur_w_beats++;
				bool wlast = is_bit_one(lookup_sample(pt, iface.signals, "wlast"));
				if (wlast) {
					if (!expected_w_burst_lens.empty()) {
						uint64_t exp = expected_w_burst_lens.front();
						expected_w_burst_lens.pop_front();
						if (cur_w_beats != exp) {
							const std::string &wlast_sig = iface.signals.at("wlast");
							push_violation(summary, iface, "AXI_WLAST_MISMATCH", pt.tick, pt.timestamp,
								       std::format("`{}` asserted on beat {} (expected {})",
										   wlast_sig, cur_w_beats, exp),
								       {wlast_sig});
						}
					}
					cur_w_beats = 0;
					pending_w_last++;
				} else if (!expected_w_burst_lens.empty() && cur_w_beats >= expected_w_burst_lens.front()) {
					const std::string &wlast_sig = iface.signals.at("wlast");
					push_violation(summary, iface, "AXI_WLAST_MISMATCH", pt.tick, pt.timestamp,
						       std::format("`{}` not asserted on final beat {} of burst",
								   wlast_sig, expected_w_burst_lens.front()),
						       {wlast_sig});
					expected_w_burst_lens.pop_front();
					cur_w_beats = 0;
					pending_w_last++;
				}
			}
		}

		if (b_valid) {
			if (pending_aw == 0 || pending_w_last == 0) {
				if (!reported_orphan_b) {
					const std::string &bvalid_sig = iface.signals.at("bvalid");
					push_violation(summary, iface, "AXI_ORPHAN_BVALID", pt.tick, pt.timestamp,
						       std::format("`{}` asserted with no outstanding write transaction", bvalid_sig),
						       {bvalid_sig});
					reported_orphan_b = true;
				}
			} else {
				reported_orphan_b = false;
			}
		} else {
			reported_orphan_b = false;
		}
		if (b_hs) {
			if (pending_aw > 0) pending_aw--;
			if (pending_w_last > 0) pending_w_last--;
		}

		if (ar_hs) {
			pending_ar++;
			if (variant_ == axi_variant::axi4) {
				auto len = parse_vcd_uint(lookup_sample(pt, iface.signals, "arlen")).value_or(0);
				expected_r_burst_lens.push_back(len + 1);
			}
		}

		if (r_valid) {
			if (pending_ar == 0) {
				if (!reported_orphan_r) {
					const std::string &rvalid_sig = iface.signals.at("rvalid");
					push_violation(summary, iface, "AXI_ORPHAN_RVALID", pt.tick, pt.timestamp,
						       std::format("`{}` asserted with no outstanding read transaction", rvalid_sig),
						       {rvalid_sig});
					reported_orphan_r = true;
				}
			} else {
				reported_orphan_r = false;
			}
		} else {
			reported_orphan_r = false;
		}

		if (r_hs) {
			if (variant_ == axi_variant::axi_lite) {
				if (pending_ar > 0) pending_ar--;
			} else {
				cur_r_beats++;
				bool rlast = is_bit_one(lookup_sample(pt, iface.signals, "rlast"));
				if (rlast) {
					if (!expected_r_burst_lens.empty()) {
						uint64_t exp = expected_r_burst_lens.front();
						expected_r_burst_lens.pop_front();
						if (cur_r_beats != exp) {
							const std::string &rlast_sig = iface.signals.at("rlast");
							push_violation(summary, iface, "AXI_RLAST_MISMATCH", pt.tick, pt.timestamp,
								       std::format("`{}` asserted on beat {} (expected {})",
										   rlast_sig, cur_r_beats, exp),
								       {rlast_sig});
						}
					}
					cur_r_beats = 0;
					if (pending_ar > 0) pending_ar--;
				} else if (!expected_r_burst_lens.empty() && cur_r_beats >= expected_r_burst_lens.front()) {
					const std::string &rlast_sig = iface.signals.at("rlast");
					push_violation(summary, iface, "AXI_RLAST_MISMATCH", pt.tick, pt.timestamp,
						       std::format("`{}` not asserted on final beat {} of burst",
								   rlast_sig, expected_r_burst_lens.front()),
						       {rlast_sig});
					expected_r_burst_lens.pop_front();
					cur_r_beats = 0;
					if (pending_ar > 0) pending_ar--;
				}
			}
		}

		prev_active_pt = &pt;
	}

	return summary;
}

} // namespace verilog
