#include "apply_text_filter.h"
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/ai_agent.h"
#include "../../filter_registry.h"
#include <nlohmann/json.hpp>

namespace tools
{

nlohmann::json apply_text_filter_validator::get_parameters_schema() const
{
	return {{"type", "object"},
		{"properties",
		 {{"text", {{"type", "string"}, {"description", "The input text to convert."}}},
		  {"filter",
		   {{"type", "string"},
		    {"description", "The name of the filter to apply (e.g., 'strip_utf8', 'strip_ansi', 'html_to_markdown')."}}},
		  {"output_path",
		   {{"type", "string"},
		    {"description", "Optional. The relative file path under the project workspace to save the converted output."}}}}},
		{"required", {"text", "filter"}}};
}

bool apply_text_filter_validator::is_allowed_in_plan_mode(const nlohmann::json &args, const agentlib::tool_context &ctx) const
{
	if (!args.contains("output_path")) return true;
	if (!args["output_path"].is_string()) return false;
	std::string path = args["output_path"].get<std::string>();
	if (path.starts_with("tmp://") || path.starts_with("tmp:/")) return true;
	if (!ctx.active_agent) return false;
	std::string plan_file = ctx.active_agent->get_plan_file();
	return !plan_file.empty() && path == plan_file;
}

bool apply_text_filter_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx,
						     std::string &out_error) const
{
	if (!args.is_object()) {
		out_error = "Arguments must be a JSON object.";
		return false;
	}

	if (!args.contains("text") || !args["text"].is_string()) {
		out_error = "Missing or invalid 'text' argument (must be string).";
		return false;
	}

	if (!args.contains("filter") || !args["filter"].is_string()) {
		out_error = "Missing or invalid 'filter' argument (must be string).";
		return false;
	}

	std::string text = args["text"].get<std::string>();
	std::string filter = args["filter"].get<std::string>();

	// Validate filter existence
	if (!agentlib::filter_registry::get_instance().has_filter(filter)) {
		std::vector<std::string> available = agentlib::filter_registry::get_instance().get_registered_filters();
		std::string list_str;
		for (size_t i = 0; i < available.size(); ++i) {
			if (i > 0) list_str += ", ";
			list_str += available[i];
		}
		out_error = "Invalid filter name '" + filter + "'. Available filters: [" + list_str + "]";
		return false;
	}

	std::string output_path;
	std::string safe_output_path;
	if (args.contains("output_path")) {
		if (!args["output_path"].is_string()) {
			out_error = "Invalid 'output_path' argument (must be string).";
			return false;
		}
		output_path = args["output_path"].get<std::string>();
		if (output_path.empty()) {
			out_error = "output_path cannot be empty.";
			return false;
		}
		// Stage 1 security verification for file write access
		if (!ctx.fs_security.validate_access(output_path, agentlib::access_type::write, safe_output_path, out_error)) {
			return false;
		}
	}

	// Validate unexpected arguments
	for (auto it = args.begin(); it != args.end(); ++it) {
		if (it.key() != "text" && it.key() != "filter" && it.key() != "output_path") {
			out_error = "Unexpected parameter '" + it.key() + "' passed to tool.";
			return false;
		}
	}

	parsed_args_.text = text;
	parsed_args_.filter = filter;
	parsed_args_.output_path = output_path;
	parsed_args_.safe_output_path = safe_output_path;

	return true;
}

std::unique_ptr<agentlib::llm_tool> apply_text_filter_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<apply_text_filter_tool>(parsed_args_);
}

REGISTER_TOOL(apply_text_filter_validator)

} // namespace tools
