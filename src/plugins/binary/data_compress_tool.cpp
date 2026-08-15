#include "data_compress_tool.h"
#include "binary_utils.h"

namespace tools {

data_compress_tool::data_compress_tool(data_compress_args args)
    : llm_tool_action("Compressing data"), args_(std::move(args))
{
}

bool data_compress_tool::validate_runtime(const agentlib::tool_context &, std::string &) const
{
	return true;
}

std::string data_compress_tool::execute(agentlib::tool_context &ctx)
{
	try {
		std::vector<uint8_t> raw_data;
		if (!args_.path.empty()) {
			std::string target_p = !args_.safe_path.empty() ? args_.safe_path : args_.path;
			raw_data = binary_utils::resolve_input_file(target_p, 0, -1, ctx.fs_security.get_vfs());
		} else {
			raw_data = binary_utils::resolve_input_data(args_.input_data, 0, -1, ctx.fs_security.get_vfs());
		}
		if (raw_data.empty()) {
            set_failure(ctx, "Empty input data.");
			return "Error: could not resolve or read input data (result was empty).";
		}
		std::vector<uint8_t> compressed = binary_utils::compress_data(raw_data, args_.format);
        set_success(ctx, "Data compressed successfully.");
		std::string out_p = !args_.safe_output_path.empty() ? args_.safe_output_path : args_.output_path;
		return binary_utils::format_binary_output(compressed, args_.output_format, out_p, ctx.fs_security.get_vfs());
	} catch (const std::exception &e) {
        set_failure(ctx, std::string("Error during compression: ") + e.what());
		return std::string("Error during compression: ") + e.what();
	}
}

}
