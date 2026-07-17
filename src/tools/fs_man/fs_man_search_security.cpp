#include "fs_man_search.h"
#include "../../agentlib/tool_registry.h"
#include <cctype>

namespace tools {

bool fs_man_search_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
	try {
		if (!args.contains("query") || !args["query"].is_string()) {
			out_error = "Missing or invalid 'query' parameter.";
			return false;
		}

		std::string query = args["query"].get<std::string>();
		if (query.empty()) {
			out_error = "'query' parameter cannot be empty.";
			return false;
		}

		// Ensure search query is safe from shell command injection
		for (char c : query) {
			if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.' && c != ' ') {
				out_error = "Invalid search query: contains unsafe characters. Only alphanumeric characters, spaces, dots, hyphens, and underscores are allowed.";
				return false;
			}
		}

		args_.query = query;
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

			for (char c : sec) {
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
					out_error = "Invalid section parameter: contains unsafe characters.";
					return false;
				}
			}
			args_.section = sec;
		}

		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> fs_man_search_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
	return std::make_unique<fs_man_search_tool>(args_);
}

REGISTER_TOOL(fs_man_search_validator)

} // namespace tools
