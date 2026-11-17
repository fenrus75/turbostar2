#pragma once

#include "plugins/verilog/vcd_protocol_checker.h"

namespace verilog {

/**
 * @brief AXI protocol family variant handled by vcd_axi_protocol_checker.
 */
enum class axi_variant {
	axi_stream,
	axi_lite,
	axi4
};

/**
 * @brief Post-simulation VCD protocol checker for AXI4-Stream, AXI4-Lite, and full AXI4 buses.
 */
class vcd_axi_protocol_checker final : public vcd_protocol_checker {
    public:
	explicit vcd_axi_protocol_checker(axi_variant variant) noexcept;

	[[nodiscard]] std::string_view name() const noexcept override;
	[[nodiscard]] std::vector<protocol_interface> discover_interfaces(const vcd_parser &parser) const override;
	[[nodiscard]] protocol_interface_summary check(const vcd_parser &parser,
						       const protocol_interface &iface) const override;

    private:
	axi_variant variant_;
};

} // namespace verilog
