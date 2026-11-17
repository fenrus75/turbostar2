#pragma once

#include "plugins/verilog/vcd_parser.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace verilog {

/**
 * @brief Describes an auto-discovered (or explicitly bound) bus interface in a VCD trace.
 */
struct protocol_interface {
	std::string protocol;
	std::string interface_name;
	std::string scope;
	std::string prefix;
	std::string clock_signal;
	std::string reset_signal;
	bool reset_active_low{true};
	std::map<std::string, std::string> signals; // role -> full VCD signal name
};

/**
 * @brief A single protocol rule violation detected at a specific clock cycle.
 */
struct protocol_violation {
	std::string protocol;
	std::string interface_name;
	std::string rule_id;
	size_t cycle{0};
	uint64_t timestamp{0};
	std::string description;
	std::vector<std::string> signals;
};

/**
 * @brief Per-interface protocol check summary and violations.
 */
struct protocol_interface_summary {
	protocol_interface iface;
	size_t cycles_checked{0};
	size_t transactions{0};
	size_t stall_cycles{0};
	std::vector<protocol_violation> violations;
};

/**
 * @brief Aggregate protocol analysis report across all discovered interfaces in a VCD trace.
 */
struct protocol_report {
	std::vector<protocol_interface_summary> interfaces;

	[[nodiscard]] size_t total_violations() const noexcept
	{
		size_t total = 0;
		for (const auto &iface : interfaces) {
			total += iface.violations.size();
		}
		return total;
	}
};

/*

# subclasses of vcd_protocol_checker

| subclass                 | filename                                         |
| ------------------------ | ------------------------------------------------ |
| vcd_axi_protocol_checker | src/plugins/verilog/vcd_axi_protocol_checker.h   |

*/
/**
 * @brief Abstract plugin interface for post-simulation VCD bus protocol checkers.
 */
class vcd_protocol_checker {
    public:
	virtual ~vcd_protocol_checker() = default;

	[[nodiscard]] virtual std::string_view name() const noexcept = 0;
	[[nodiscard]] virtual std::vector<protocol_interface> discover_interfaces(const vcd_parser &parser) const = 0;
	[[nodiscard]] virtual protocol_interface_summary check(const vcd_parser &parser,
							       const protocol_interface &iface) const = 0;
};

} // namespace verilog
