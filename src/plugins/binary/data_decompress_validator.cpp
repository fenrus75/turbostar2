#include "data_decompress_validator.h"
#include "agentlib/tool_registry.h"

namespace tools {

std::string data_decompress_validator::get_description() const {
	return "Extract and decompress data from various sources (files, data URLs, hex/base64 strings).";
}

nlohmann::json data_decompress_validator::get_parameters_schema() const {
	return {
	    {"type", "object"},
	    {"properties",
	     {{"path", {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.bin'). Input file to decompress. Mutually exclusive with 'input_data'."}}},
	      {"input_file", {{"type", "string"}, {"description", "Legacy alias for 'path'."}}},
	      {"input_data", {{"type", "string"}, {"description", "Raw data string, hex, base64, or data URL to decompress. Mutually exclusive with 'path'."}}},
	      {"format", {{"type", "string"}, {"description", "Compression format. 'deflate' is an alias for 'zlib', 'none' bypasses decompression (just copy/passthrough)."}, {"enum", {"auto", "zstd", "gzip", "zlib", "deflate", "xz", "bzip2", "lz4", "pdflzw", "lzw", "pdfrunlength", "runlength", "ascii85", "none"}}, {"default", "auto"}}},
	      {"output_format", {{"type", "string"}, {"description", "Return format."}, {"enum", {"hex", "base64", "text"}}, {"default", "text"}}},
	      {"output_path", {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://out.txt'). Optional file path to write output instead of returning it."}}},
	      {"output_file", {{"type", "string"}, {"description", "Legacy alias for 'output_path'."}}},
	      {"offset", {{"type", "integer"}, {"description", "Byte offset to start reading from."}, {"default", 0}}},
	      {"length", {{"type", "integer"}, {"description", "Maximum number of bytes to read."}, {"default", -1}}}}},
	    {"required", nlohmann::json::array()}};
}

bool data_decompress_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &, std::string &out_error) const {
	try {
		args_.input_data = raw_json.value("input_data", "");
		args_.path = raw_json.value("path", "");
		args_.input_file = raw_json.value("input_file", "");
		args_.format = raw_json.value("format", "auto");
		args_.output_format = raw_json.value("output_format", "text");
		args_.output_path = raw_json.value("output_path", "");
		args_.output_file = raw_json.value("output_file", "");
		args_.offset = raw_json.value("offset", 0);
		args_.length = raw_json.value("length", -1);
		
		std::string target_input_file = !args_.path.empty() ? args_.path : args_.input_file;
		bool has_data = !args_.input_data.empty();
		bool has_file = !target_input_file.empty();
		if (!has_data && !has_file) {
			out_error = "Must specify exactly one of 'path' or 'input_data'.";
			return false;
		}
		if (has_data && has_file) {
			out_error = "Parameters 'input_data' and 'path' are mutually exclusive.";
			return false;
		}
		
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> data_decompress_validator::create_tool_impl(const nlohmann::json &) const {
	return std::make_unique<data_decompress_tool>(args_);
}

}

extern "C" {
void register_data_decompress(void) {
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::data_decompress_validator>(); });
}
void unregister_data_decompress(void) {
	agentlib::tool_registry::get_instance().unregister_validator("data_decompress");
}
}
