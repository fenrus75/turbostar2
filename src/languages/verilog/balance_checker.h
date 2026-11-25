#pragma once

#include "languages/balance_checker.h"

#include <span>
#include <string>
#include <vector>

namespace languages::verilog
{

/**
 * @brief Verilog / SystemVerilog block and concatenation balance checker.
 *
 * Tracks begin/end, case/casex/casez/endcase, module/macromodule/endmodule,
 * and {/} concatenations while skipping comments, double-quoted strings,
 * escaped identifiers (\foo ), and unpaired single-quote literals (1'b0, 8'hFF).
 */
class balance_checker {
public:
	static std::vector<languages::balance_entry> calculate_balance(std::span<const std::string> lines, int start_line_0,
								       int end_line_0);
};

} // namespace languages::verilog
