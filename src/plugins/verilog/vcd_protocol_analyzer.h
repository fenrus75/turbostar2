#pragma once

#include "plugins/verilog/vcd_protocol_checker.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace verilog {

/**
 * @brief Coordinates registered vcd_protocol_checker plugins on parsed VCD traces.
 */
class vcd_protocol_analyzer {
    public:
	vcd_protocol_analyzer();

	void register_checker(std::unique_ptr<vcd_protocol_checker> checker);

	[[nodiscard]] protocol_report analyze(const vcd_parser &parser) const;

	[[nodiscard]] static std::vector<const protocol_violation *>
	find_violations_near_tick(const protocol_report &report, size_t tick, size_t window = 5);

	[[nodiscard]] static std::string format_interfaces_summary(const protocol_report &report);

	[[nodiscard]] static std::string format_violations_markdown(const protocol_report &report,
								    std::string_view vcd_path,
								    bool is_cli_mode,
								    size_t max_show = 5);

    private:
	std::vector<std::unique_ptr<vcd_protocol_checker>> checkers_;
};

} // namespace verilog
