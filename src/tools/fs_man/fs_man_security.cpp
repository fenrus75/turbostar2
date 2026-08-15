#include "fs_man.h"
#include "agentlib/tool_registry.h"

namespace tools {

bool fs_man_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const {
	try {
		if (!args.contains("name") || !args["name"].is_string()) {
			out_error = "Missing or invalid 'name' parameter.";
			return false;
		}

		std::string name = args["name"].get<std::string>();
		if (name.empty()) {
			out_error = "'name' parameter cannot be empty.";
			return false;
		}

		if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name.find("..") != std::string::npos) {
			out_error = "Invalid name parameter: cannot contain path traversal or directory separators.";
			return false;
		}

		args_.name = name;
		args_.section = std::nullopt;

		if (args.contains("section")) {
			std::string sec;
			if (args["section"].is_string()) {
				sec = args["section"].get<std::string>();
			} else if (args["section"].is_number_integer()) {
				sec = std::to_string(args["section"].get<int>());
			} else {
				out_error = "Invalid 'section' parameter: must be a string or integer.";
				return false;
			}

			if (sec.find('/') != std::string::npos || sec.find('\\') != std::string::npos || sec.find("..") != std::string::npos) {
				out_error = "Invalid section parameter: cannot contain path traversal or directory separators.";
				return false;
			}
			args_.section = sec;
		}

		args_.filter.clear();
		if (args.contains("filter")) {
			if (!args["filter"].is_string()) {
				out_error = "Invalid 'filter' parameter: must be a string.";
				return false;
			}
			std::string filter = args["filter"].get<std::string>();
			if (filter.empty()) {
				out_error = "'filter' parameter cannot be empty.";
				return false;
			}
			args_.filter = filter;
		}

		args_.output_path.clear();
		args_.safe_output_path.clear();
		if (args.contains("output_path")) {
			if (!args["output_path"].is_string()) {
				out_error = "Invalid 'output_path' parameter: must be a string.";
				return false;
			}
			std::string output_path = args["output_path"].get<std::string>();
			if (output_path.empty()) {
				out_error = "'output_path' parameter cannot be empty.";
				return false;
			}
			// Stage 1 security verification for file write access
			if (!ctx.fs_security.validate_access(output_path, agentlib::access_type::write, args_.safe_output_path, out_error)) {
				return false;
			}
			args_.output_path = output_path;
		}

		for (auto it = args.begin(); it != args.end(); ++it) {
			if (it.key() != "name" && it.key() != "section" && it.key() != "filter" && it.key() != "output_path") {
				out_error = "Unexpected parameter '" + it.key() + "' passed to tool.";
				return false;
			}
		}

		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> fs_man_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
	return std::make_unique<fs_man_tool>(args_);
}

REGISTER_TOOL(fs_man_validator)

} // namespace tools
