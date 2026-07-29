#include "data_decompress_tool.h"
#include "binary_utils.h"

namespace tools {

data_decompress_tool::data_decompress_tool(data_decompress_args args)
    : llm_tool_action("Decompressing data"), args_(std::move(args))
{
}

bool data_decompress_tool::validate_runtime(const agentlib::tool_context &, std::string &) const
{
	return true;
}

std::string data_decompress_tool::execute(agentlib::tool_context &ctx)
{
	try {
		std::vector<uint8_t> raw_data;
		if (!args_.path.empty()) {
			raw_data = binary_utils::resolve_input_file(args_.path, args_.offset, args_.length, ctx.fs_security.get_vfs());
		} else {
			raw_data = binary_utils::resolve_input_data(args_.input_data, args_.offset, args_.length, ctx.fs_security.get_vfs());
		}
		if (raw_data.empty()) {
            set_failure(ctx, "Empty input data.");
			return "Error: could not resolve or read input data (result was empty). Check your offset and length.";
		}
		std::vector<uint8_t> decompressed = binary_utils::decompress_data(raw_data, args_.format);
        set_success(ctx, "Data decompressed successfully.");
		return binary_utils::format_binary_output(decompressed, args_.output_format, args_.output_path);
	} catch (const std::exception &e) {
        set_failure(ctx, std::string("Error during decompression: ") + e.what());
		return std::string("Error during decompression: ") + e.what();
	}
}

}
