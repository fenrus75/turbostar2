#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <sstream>
#include "../../agentlib/tool_registry.h"
#include "list_tool_calls.h"

namespace tools
{

list_tool_calls_tool::list_tool_calls_tool(list_tool_calls_args args)
    : llm_tool_action("Listing available tool schemas"), args_(std::move(args))
{
}

bool list_tool_calls_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string list_tool_calls_tool::execute(agentlib::tool_context &ctx)
{
	auto tools_json = agentlib::tool_registry::get_instance().get_tools_json();

	std::ostringstream oss;
	if (!args_.show_details) {
		oss << "| Tool Name | Description |\n";
		oss << "|-----------|-------------|\n";
	}

	size_t count = 0;
	for (const auto &tool_node : tools_json) {
		if (tool_node.contains("function")) {
			auto func = tool_node["function"];
			std::string name = func.value("name", "unknown");
			std::string desc = func.value("description", "");

			// 1. Filter by search query if specified
			if (!args_.search.empty()) {
				std::string name_lower = name;
				std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
				std::string search_lower = args_.search;
				std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
				if (name_lower.find(search_lower) == std::string::npos) {
					continue;
				}
			}

			// Clean up description for markdown table or list formatting
			for (char &c : desc) {
				if (c == '\n' || c == '\r')
					c = ' ';
				else if (c == '|')
					c = '/';
			}

			if (args_.show_details) {
				oss << "### `" << name << "`\n";
				oss << "* **Description:** " << desc << "\n";
				if (func.contains("parameters") && func["parameters"].contains("properties") && func["parameters"]["properties"].is_object()) {
					oss << "* **Arguments:**\n";
					auto props = func["parameters"]["properties"];
					nlohmann::json req_array = nlohmann::json::array();
					if (func["parameters"].contains("required") && func["parameters"]["required"].is_array()) {
						req_array = func["parameters"]["required"];
					}
					for (auto it = props.begin(); it != props.end(); ++it) {
						std::string param_name = it.key();
						auto param_info = it.value();
						std::string p_type = param_info.value("type", "unknown");
						std::string p_desc = param_info.value("description", "");
						bool is_required = false;
						for (const auto &req_item : req_array) {
							if (req_item == param_name) {
								is_required = true;
								break;
							}
						}
						std::string req_str = is_required ? "required" : "optional";
						oss << "    * `" << param_name << "` *(" << p_type << ", " << req_str << ")*: " << p_desc << "\n";
					}
				} else {
					oss << "* **Arguments:** None\n";
				}
				oss << "\n";
			} else {
				oss << "| `" << name << "` | " << desc << " |\n";
			}
			count++;
		}
	}

	set_success(ctx, std::to_string(count) + " tools");
	return oss.str();
}

} // namespace tools