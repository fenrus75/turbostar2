#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "tools/fs_read_lines/fs_batch_read.h"

namespace tools
{

class fs_batch_read_validator : public agentlib::tool_validator
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
		return "fs_batch_read";
	}

	std::string get_description() const override
	{
		return "Reads multiple files or code chunks in a single batched operation. Supports reading line ranges ('lines'), symbol "
		       "definitions ('symbol'), or class context previews ('class_context'). Suppresses codemap overviews when batch size "
		       "> 1 to preserve context tokens.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"items",
		       {{"type", "array"},
			{"description",
			 "List of read requests. Each item specifies a path and read type ('lines', 'symbol', or 'class_context')."},
			{"items",
			 {{"type", "object"},
			  {"properties",
			   {{"path",
			     {{"type", "string"},
			      {"description", "Relative path under the project workspace or VFS URI (e.g., 'src/mime.h')."}}},
			    {"type",
			     {{"type", "string"},
			      {"enum", {"lines", "symbol", "class_context"}},
			      {"description", "Type of read operation: 'lines' (default), 'symbol', or 'class_context'."}}},
			    {"start",
			     {{"type", "integer"},
			      {"description", "1-based line number to start reading from (for type 'lines'). Default 1."}}},
			    {"end",
			     {{"type", "integer"},
			      {"description", "1-based line number to end reading at (inclusive, for type 'lines')."}}},
			    {"length",
			     {{"type", "integer"},
			      {"description", "Number of lines to read starting from 'start'. Mutually exclusive with 'end' and 'tail'."}}},
			    {"tail",
			     {{"type", "integer"},
			      {"description",
			       "Number of lines to read from the end of the file. Mutually exclusive with start/end/length."}}},
			    {"name",
			     {{"type", "string"},
			      {"description", "Symbol name (for type 'symbol') or optional class name (for type 'class_context')."}}}}}},
			 {"required", nlohmann::json::array({"path"})}}}}}},
		    {"required", nlohmann::json::array({"items"})}};
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override
	{
		return {{"files", "items"}, {"chunks", "items"}, {"requests", "items"}};
	}

	std::vector<agentlib::tool_example> get_examples() const override
	{
		return {{"Batch Read Multiple Chunks and Symbols",
			 nlohmann::json{{"items",
					 {{{"path", "src/mime.h"}, {"type", "lines"}, {"start", 1}, {"end", 50}},
					  {{"path", "src/mime.cpp"}, {"type", "symbol"}, {"name", "from_extension"}},
					  {{"path", "src/mime.cpp"}, {"type", "class_context"}}}}},
			 "Reads lines 1-50 of src/mime.h, the definition of from_extension in src/mime.cpp, and class context for "
			 "src/mime.cpp in a single batched call."},
			{"Batch Read Multiple Files",
			 nlohmann::json{{"items",
					 {{{"path", "tests/unit/poem.txt"}, {"start", 1}, {"end", 5}},
					  {{"path", "tests/unit/poem.txt"}, {"tail", 3}}}}},
			 "Reads the first 5 lines and the last 3 lines of tests/unit/poem.txt."}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &untrusted_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			args_.items.clear();

			nlohmann::json untrusted_items;
			if (untrusted_json.contains("items") && untrusted_json["items"].is_array()) {
				untrusted_items = untrusted_json["items"];
			} else if (untrusted_json.contains("files") && untrusted_json["files"].is_array()) {
				untrusted_items = untrusted_json["files"];
			} else if (untrusted_json.contains("path") && untrusted_json["path"].is_string()) {
				// Convenience fallback: single item passed at top level
				untrusted_items = nlohmann::json::array({untrusted_json});
			} else {
				out_error = "Missing required parameter 'items' (must be an array).";
				return false;
			}

			if (untrusted_items.empty()) {
				out_error = "'items' array cannot be empty.";
				return false;
			}

			if (untrusted_items.size() > 50) {
				out_error = "Batch size cannot exceed 50 items.";
				return false;
			}

			for (const auto &untrusted_item : untrusted_items) {
				batch_read_item item;

				if (!untrusted_item.is_object()) {
					item.has_error = true;
					item.error_message = "Error: Each batch item must be a JSON object.";
					args_.items.push_back(std::move(item));
					continue;
				}

				std::string untrusted_path;
				if (untrusted_item.contains("path") && untrusted_item["path"].is_string()) {
					untrusted_path = untrusted_item["path"].get<std::string>();
				} else if (untrusted_item.contains("file") && untrusted_item["file"].is_string()) {
					untrusted_path = untrusted_item["file"].get<std::string>();
				} else if (untrusted_item.contains("filename") && untrusted_item["filename"].is_string()) {
					untrusted_path = untrusted_item["filename"].get<std::string>();
				}

				if (untrusted_path.empty()) {
					item.has_error = true;
					item.error_message = "Error: Item missing required 'path' parameter.";
					args_.items.push_back(std::move(item));
					continue;
				}

				item.requested_path = untrusted_path;

				std::string untrusted_check_path = untrusted_path;
				if (untrusted_check_path.starts_with("file://")) {
					untrusted_check_path = untrusted_check_path.substr(7);
				}

				std::string canonical_path;
				std::string path_error;
				if (!ctx.fs_security.validate_access(untrusted_check_path, agentlib::access_type::read, canonical_path,
								     path_error)) {
					item.has_error = true;
					item.error_message = "Error: " + path_error;
					args_.items.push_back(std::move(item));
					continue;
				}
				item.safe_path = canonical_path;

				// Parse type
				std::string untrusted_type_str = "lines";
				if (untrusted_item.contains("type") && untrusted_item["type"].is_string()) {
					untrusted_type_str = untrusted_item["type"].get<std::string>();
				}

				if (untrusted_type_str == "lines") {
					item.type = batch_item_type::lines;
				} else if (untrusted_type_str == "symbol") {
					item.type = batch_item_type::symbol;
				} else if (untrusted_type_str == "class_context") {
					item.type = batch_item_type::class_context;
				} else {
					item.has_error = true;
					item.error_message = std::format(
					    "Error: Unknown read type '{}'. Valid types are 'lines', 'symbol', 'class_context'.",
					    untrusted_type_str);
					args_.items.push_back(std::move(item));
					continue;
				}

				// Extract name for symbol or class_context
				if (untrusted_item.contains("name") && untrusted_item["name"].is_string()) {
					item.name = untrusted_item["name"].get<std::string>();
				} else if (untrusted_item.contains("symbol") && untrusted_item["symbol"].is_string()) {
					item.name = untrusted_item["symbol"].get<std::string>();
				} else if (untrusted_item.contains("symbol_name") && untrusted_item["symbol_name"].is_string()) {
					item.name = untrusted_item["symbol_name"].get<std::string>();
				} else if (untrusted_item.contains("class_name") && untrusted_item["class_name"].is_string()) {
					item.name = untrusted_item["class_name"].get<std::string>();
				}

				if (item.type == batch_item_type::symbol && item.name.empty()) {
					item.has_error = true;
					item.error_message = "Error: Parameter 'name' is required for type 'symbol'.";
					args_.items.push_back(std::move(item));
					continue;
				}

				// Extract range parameters
				int start_line = -1;
				int end_line = -1;
				int tail = -1;
				int length = -1;

				if (untrusted_item.contains("start") && untrusted_item["start"].is_number_integer()) {
					start_line = untrusted_item["start"].get<int>();
				} else if (untrusted_item.contains("start_line") && untrusted_item["start_line"].is_number_integer()) {
					start_line = untrusted_item["start_line"].get<int>();
				}

				if (untrusted_item.contains("end") && untrusted_item["end"].is_number_integer()) {
					end_line = untrusted_item["end"].get<int>();
				} else if (untrusted_item.contains("end_line") && untrusted_item["end_line"].is_number_integer()) {
					end_line = untrusted_item["end_line"].get<int>();
				}

				if (untrusted_item.contains("tail") && untrusted_item["tail"].is_number_integer()) {
					tail = untrusted_item["tail"].get<int>();
				}

				if (untrusted_item.contains("length") && untrusted_item["length"].is_number_integer()) {
					length = untrusted_item["length"].get<int>();
				} else if (untrusted_item.contains("lines") && untrusted_item["lines"].is_number_integer()) {
					length = untrusted_item["lines"].get<int>();
				} else if (untrusted_item.contains("num_lines") && untrusted_item["num_lines"].is_number_integer()) {
					length = untrusted_item["num_lines"].get<int>();
				} else if (untrusted_item.contains("line_count") && untrusted_item["line_count"].is_number_integer()) {
					length = untrusted_item["line_count"].get<int>();
				}

				if (tail != -1) {
					if (start_line != -1 || end_line != -1 || length != -1) {
						item.has_error = true;
						item.error_message =
						    "Error: 'tail' cannot be used together with 'start', 'end', or 'length'.";
						args_.items.push_back(std::move(item));
						continue;
					}
					if (tail <= 0) {
						item.has_error = true;
						item.error_message = "Error: 'tail' parameter must be greater than 0.";
						args_.items.push_back(std::move(item));
						continue;
					}
					item.tail = tail;
					item.length = std::nullopt;
					item.start_line = 1;
					item.end_line = 1000000;
				} else if (length != -1) {
					if (end_line != -1) {
						item.has_error = true;
						item.error_message =
						    "Error: 'length' cannot be used together with 'end'. Specify either 'end' or 'length'.";
						args_.items.push_back(std::move(item));
						continue;
					}
					if (length <= 0) {
						item.has_error = true;
						item.error_message = "Error: 'length' parameter must be greater than 0.";
						args_.items.push_back(std::move(item));
						continue;
					}
					item.tail = std::nullopt;
					item.length = length;
					item.start_line = (start_line == -1) ? 1 : start_line;
					if (item.start_line < 1)
						item.start_line = 1;
					int64_t calc_end = static_cast<int64_t>(item.start_line) + length - 1;
					item.end_line = static_cast<int>(std::min<int64_t>(calc_end, 10000000));
				} else {
					item.tail = std::nullopt;
					item.length = std::nullopt;
					item.start_line = (start_line == -1) ? 1 : start_line;
					item.end_line = (end_line == -1) ? 1000000 : end_line;

					if (item.start_line < 1)
						item.start_line = 1;
					if (item.end_line < item.start_line) {
						item.has_error = true;
						item.error_message = "Error: end line cannot be less than start line.";
						args_.items.push_back(std::move(item));
						continue;
					}
				}

				args_.items.push_back(std::move(item));
			}

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<fs_batch_read_tool>(args_);
	}

      private:
	mutable fs_batch_read_args args_;
};

REGISTER_TOOL(fs_batch_read_validator)

} // namespace tools
