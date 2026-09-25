#include "crashdump_clear.h"
#include "crashdump_manager.h"
#include "fs_utils.h"

namespace tools
{

bool crashdump_clear_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string crashdump_clear_tool::execute(agentlib::tool_context &ctx)
{
	if (crash_id_.empty()) {
		crashdump_manager::get_instance().clear_all();
		set_success(ctx, "all crash dumps cleared");
		return fs_utils::wrap_prompt_untrusted_data_tag("crashdump_clear_result", "Successfully cleared all crash dumps.");
	}

	bool cleared = crashdump_manager::get_instance().clear_crash(crash_id_);
	if (cleared) {
		set_success(ctx, std::format("crash dump {} cleared", crash_id_));
		return fs_utils::wrap_prompt_untrusted_data_tag("crashdump_clear_result",
								std::format("Successfully cleared crash dump {}.", crash_id_));
	} else {
		set_failure(ctx, std::format("crash dump {} not found", crash_id_));
		return fs_utils::wrap_prompt_untrusted_data_tag("crashdump_clear_result",
								std::format("Crash dump {} not found.", crash_id_));
	}
}

} // namespace tools
