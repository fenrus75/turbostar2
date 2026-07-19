#include "data_compress_tool.h"
#include "binary_utils.h"

namespace tools {

data_compress_tool::data_compress_tool(data_compress_args args)
    : llm_tool_action("Compressing data"), args_(std::move(args))
{
}

bool data_compress_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	return true;
}

std::string data_compress_tool::execute(agentlib::tool_context &ctx)
{
	try {
		std::vector<uint8_t> raw_data = binary_utils::resolve_input_data(args_.input_data, 0, -1);
		if (raw_data.empty()) {
            set_failure(ctx, "Empty input data.");
			return "Error: could not resolve or read input data (result was empty).";
		}
		std::vector<uint8_t> compressed = binary_utils::compress_data(raw_data, args_.format);
        set_success(ctx, "Data compressed successfully.");
		return binary_utils::format_binary_output(compressed, args_.output_format, args_.output_file);
	} catch (const std::exception &e) {
        set_failure(ctx, std::string("Error during compression: ") + e.what());
		return std::string("Error during compression: ") + e.what();
	}
}

}
