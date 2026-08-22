#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_file_codemap.h"

namespace tools {

class fs_file_codemap_validator : public agentlib::tool_validator {
public:
	bool is_pure() const override
	{
		return true;
	}
	bool is_silent_by_default() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "fs_file_codemap";
	}
	std::string get_description() const override
	{
		return "Provide a symbol codemap overview table for a file showing functions, methods, classes, and structs along with their start line and end line numbers.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"path", {{"type", "string"}, {"description", "The path to the file, relative to the project root."}}},
			  {"min_lines", {{"type", "integer"}, {"description", "Optional. Minimum line length threshold to filter out trivial 1-line declarations (default: 1)."}}},
			  {"full", {{"type", "boolean"}, {"description", "Optional. Whether to return un-truncated whole-file symbols and section headings (default: true)."}}},
			  {"max_symbols", {{"type", "integer"}, {"description", "Optional. Maximum symbol count cap (default: 0 for unlimited)."}}}}},
			{"required", nlohmann::json::array({"path"})}};
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {
			{
				"Source File Symbol & Function Outline",
				nlohmann::json{{"path", "src/editor.cpp"}},
				"Outlines classes, methods, structs, and line bounds for src/editor.cpp without reading full file content."
			}
		};
	}


protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		if (!raw_json.contains("path") || !raw_json["path"].is_string()) {
			out_error = "Missing or invalid 'path' string parameter.";
			return false;
		}

		std::string path_arg = raw_json["path"].get<std::string>();
		if (path_arg.empty()) {
			out_error = "Path parameter cannot be empty.";
			return false;
		}

		int min_lines_arg = 1;
		if (raw_json.contains("min_lines")) {
			if (!raw_json["min_lines"].is_number_integer()) {
				out_error = "Invalid 'min_lines' integer parameter.";
				return false;
			}
			min_lines_arg = std::max(1, raw_json["min_lines"].get<int>());
		}

		bool full_arg = true;
		if (raw_json.contains("full")) {
			if (!raw_json["full"].is_boolean()) {
				out_error = "Invalid 'full' boolean parameter.";
				return false;
			}
			full_arg = raw_json["full"].get<bool>();
		}

		int max_symbols_arg = 500;
		if (raw_json.contains("max_symbols")) {
			if (!raw_json["max_symbols"].is_number_integer()) {
				out_error = "Invalid 'max_symbols' integer parameter.";
				return false;
			}
			int requested_max = raw_json["max_symbols"].get<int>();
			max_symbols_arg = (requested_max <= 0) ? 500 : std::min(1000, requested_max);
		}

		std::string check_path = path_arg;
		if (check_path.starts_with("file://")) {
			check_path = check_path.substr(7);
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(check_path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.requested_path = path_arg;
		args_.safe_path = canonical_path;
		args_.min_lines = min_lines_arg;
		args_.full = full_arg;
		args_.max_symbols = max_symbols_arg;

		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<fs_file_codemap_tool>(args_);
	}

private:
	mutable fs_file_codemap_args args_;
};

REGISTER_TOOL(fs_file_codemap_validator)

} // namespace tools
