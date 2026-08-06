#include "../../agentlib/tool_registry.h"
#include "fs_compile_file.h"

namespace tools
{

bool fs_compile_file_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	try {
		if (args.is_object()) {
			for (auto it = args.begin(); it != args.end(); ++it) {
				if (it.key() != "path" && it.key() != "async") {
					out_error = "Unexpected argument: " + it.key();
					return false;
				}
			}
		}
		args_ = args.get<fs_compile_file_args>();
		std::string resolved_path;
		if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, resolved_path, out_error)) {
			return false;
		}
		resolved_path_ = resolved_path;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> fs_compile_file_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<fs_compile_file_tool>(args_, resolved_path_);
}

REGISTER_TOOL(fs_compile_file_validator)

} // namespace tools
