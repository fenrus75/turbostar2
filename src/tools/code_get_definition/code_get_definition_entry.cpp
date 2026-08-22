#include "fs_utils.h"
#include "project_manager.h"
#include "code_get_definition.h"

namespace tools
{

std::string code_get_definition_tool::execute(agentlib::tool_context &ctx)
{
	std::string safe_path;
	std::string error;
	if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, safe_path, error)) {
		return fs_utils::wrap_prompt_untrusted_data_tag("code_get_definition_result", "Error: " + error);
	}

	if (!project_manager::get_instance().lsp_is_supported_file(safe_path)) {
		return fs_utils::wrap_prompt_untrusted_data_tag("code_get_definition_result", "Error: LSP is not supported for this file type.");
	}

	auto locations = project_manager::get_instance().lsp_query_definition(safe_path, args_.line - 1, args_.character);
	if (locations.empty()) {
		return fs_utils::wrap_prompt_untrusted_data_tag("code_get_definition_result", "No definition found.");
	}

	nlohmann::json result = nlohmann::json::array();
	for (const auto &loc : locations) {
		std::string resolved_out_path;
		std::string out_err;
		// SECURITY CHECK: Sanitize LSP output paths against file security allowlist
		if (!ctx.fs_security.validate_access(loc.path, agentlib::access_type::read, resolved_out_path, out_err)) {
			continue; // Skip external/unauthorized paths outside workspace
		}

		std::string display_path = fs_utils::make_relative_to_project(resolved_out_path);

		result.push_back({{"path", display_path},
				  {"start_line", loc.range.start_y + 1},
				  {"start_character", loc.range.start_x},
				  {"end_line", loc.range.end_y + 1},
				  {"end_character", loc.range.end_x}});
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("code_get_definition_result", result.dump(2));
}

class code_get_definition_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "code_get_definition";
	}
	std::string get_description() const override
	{
		return "Finds the definition(s) of a symbol at a specific location.";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"path", {{"type", "string"}, {"description", "The file path."}}},
			  {"line", {{"type", "integer"}, {"description", "The 1-based line number."}}},
			  {"character", {{"type", "integer"}, {"description", "The 0-based character offset."}}}}},
			{"required", {"path", "line", "character"}}};
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {
			{
				"Go to Definition for Symbol at Line/Character Coordinates",
				nlohmann::json{
					{"path", "src/editor.cpp"},
					{"line", 45},
					{"character", 12}
				},
				"Queries LSP for definition of symbol at 1-based line 45 and 0-based character index 12 in src/editor.cpp."
			}
		};
	}

	bool is_pure() const override

	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context & /*ctx*/, std::string &out_error) const override
	{
		if (!args.is_object() || !args.contains("path") || !args.contains("line") || !args.contains("character")) {
			out_error = "Missing required arguments.";
			return false;
		}
		for (auto it = args.begin(); it != args.end(); ++it) {
			if (it.key() != "path" && it.key() != "line" && it.key() != "character") {
				out_error = "Unexpected property: " + it.key();
				return false;
			}
		}
		int line = args["line"].get<int>();
		int character = args["character"].get<int>();
		if (line < 1) {
			out_error = "Security Violation: Line number must be >= 1.";
			return false;
		}
		if (character < 0) {
			out_error = "Security Violation: Character offset must be >= 0.";
			return false;
		}
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override
	{
		code_get_definition_args parsed_args;
		parsed_args.path = args["path"].get<std::string>();
		parsed_args.line = args["line"].get<int>();
		parsed_args.character = args["character"].get<int>();
		return std::make_unique<code_get_definition_tool>(std::move(parsed_args));
	}
};

REGISTER_TOOL(code_get_definition_validator);

} // namespace tools
