#include <nlohmann/json.hpp>
#include <optional>
#include "../../agentlib/json_utils.h"
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_read_lines.h"

namespace tools
{

class fs_read_lines_validator : public agentlib::tool_validator
{
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
		return "fs_read_lines";
	}
	std::string get_description() const override
	{
		return "Reads a specific range of text lines from a file. Output lines are prefixed with their 1-based line number in '<line_number>: <line_text>' format. Automatically appends a compact symbol codemap overview table when reading a partial range of a source or header file.";;
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path", {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt')."}}},
		      {"start_line",
		       {{"type", "integer"}, {"description", "The 1-based line number to start reading from. Defaults to 1 if omitted. Mutually exclusive with 'tail'."}}},
		      {"end_line",
		       {{"type", "integer"},
			{"description", "The 1-based line number to end reading at (inclusive). Optional. Mutually exclusive with 'tail' and 'length'."}}},
		      {"length",
		       {{"type", "integer"},
			{"description", "Optional. The number of lines to read starting from 'start_line'. Mutually exclusive with 'end_line' and 'tail'."}}},
		      {"tail",
		       {{"type", "integer"},
			{"description", "Optional. The number of lines to read from the end of the file. Mutually exclusive with 'start_line', 'end_line', and 'length'."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override
	{
		return {
			{"num_lines", "length"},
			{"line_count", "length"},
			{"lines_count", "length"}
		};
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {
			{
				"Search Test Suite List for Keyword",
				nlohmann::json{{"path", "system://project/testlist.md?search=fs_grep"}},
				"Full Flow: 1) Call fs_read_lines(path='system://project/testlist.md?search=fs_grep') to discover exact test target names (e.g. 'unit_fs_grep_files') -> 2) Call fs_run_tests(test_names=['unit_fs_grep_files']) to execute."
			},
			{
				"Read Specific Line Range of Source File",
				nlohmann::json{{"path", "src/editor.cpp"}, {"start_line", 40}, {"end_line", 80}},
				"Reads lines 40 to 80 from src/editor.cpp with 1-based line numbers."
			}
		};
	}




      protected:
	// Stage 1: Pre-invocation validation
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override
	{
		try {
			std::string raw_path = raw_json.value("path", "");
			int start_line = -1;
			int end_line = -1;
			int tail = -1;
			int length = -1;

			if (!json_utils::get_number(raw_json, "start_line", start_line, -1, out_error)) return false;
			if (!json_utils::get_number(raw_json, "end_line", end_line, -1, out_error)) return false;
			if (!json_utils::get_number(raw_json, "tail", tail, -1, out_error)) return false;
			if (!json_utils::get_number(raw_json, "length", length, -1, out_error)) return false;

			if (raw_path.empty()) {
				out_error = "Path parameter cannot be empty.";
				return false;
			}

			std::string check_path = raw_path;
			if (check_path.starts_with("file://")) {
				check_path = check_path.substr(7);
			}

			// CRITICAL: Perform the file security manager check (access_type::read)
			std::string canonical_path;
			if (!ctx.fs_security.validate_access(check_path, agentlib::access_type::read, canonical_path, out_error)) {
				return false;
			}

			// Populate the strict args for the tool
			args_.requested_path = raw_path;
			args_.safe_path = canonical_path;

			if (tail != -1) {
				if (start_line != -1 || end_line != -1 || length != -1) {
					out_error = "'tail' parameter cannot be used together with 'start_line', 'end_line', or 'length'.";
					return false;
				}
				if (tail <= 0) {
					out_error = "'tail' parameter must be greater than 0.";
					return false;
				}
				args_.tail = tail;
				args_.length = std::nullopt;
				args_.start_line = 1;
				args_.end_line = 1000000;
			} else if (length != -1) {
				if (end_line != -1) {
					out_error = "'length' parameter cannot be used together with 'end_line'. Specify either 'end_line' or 'length'.";
					return false;
				}
				if (length <= 0) {
					out_error = "'length' parameter must be greater than 0.";
					return false;
				}
				args_.tail = std::nullopt;
				args_.length = length;
				args_.start_line = (start_line == -1) ? 1 : start_line;
				if (args_.start_line < 1)
					args_.start_line = 1;
				int64_t calc_end = static_cast<int64_t>(args_.start_line) + length - 1;
				args_.end_line = static_cast<int>(std::min<int64_t>(calc_end, 10000000));
			} else {
				args_.tail = std::nullopt;
				args_.length = std::nullopt;
				args_.start_line = (start_line == -1) ? 1 : start_line;
				args_.end_line = (end_line == -1) ? 1000000 : end_line;

				if (args_.start_line < 1)
					args_.start_line = 1;
				if (args_.end_line < args_.start_line) {
					out_error = "end_line cannot be less than start_line.";
					return false;
				}
			}

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		// Because tool_validators are transient (created per-request in tool_registry::execute_tool),
		// it is perfectly thread-safe to store the validated args in a mutable member variable
		// during validate_args_impl and consume them here.
		return std::make_unique<fs_read_lines_tool>(args_);
	}

      private:
	mutable fs_read_lines_args args_;
};

// Register the tool with the global registry
REGISTER_TOOL(fs_read_lines_validator)

} // namespace tools
