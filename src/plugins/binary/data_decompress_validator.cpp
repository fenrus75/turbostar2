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
	     {{"input_data", {{"type", "string"}, {"description", "The input data to decompress."}}},
	      {"format", {{"type", "string"}, {"description", "Compression format. 'deflate' is an alias for 'zlib'."}, {"enum", {"auto", "zstd", "gzip", "zlib", "deflate", "xz", "bzip2", "lz4", "pdflzw", "lzw", "pdfrunlength", "runlength", "ascii85"}}, {"default", "auto"}}},
	      {"output_format", {{"type", "string"}, {"description", "Return format."}, {"enum", {"hex", "base64", "text"}}, {"default", "text"}}},
	      {"output_file", {{"type", "string"}, {"description", "Optional file to write output."}}},
	      {"offset", {{"type", "integer"}, {"description", "Byte offset to start reading from."}, {"default", 0}}},
	      {"length", {{"type", "integer"}, {"description", "Maximum number of bytes to read."}, {"default", -1}}}}},
	    {"required", nlohmann::json::array({"input_data"})}};
}

bool data_decompress_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const {
	try {
		args_.input_data = raw_json.value("input_data", "");
		args_.format = raw_json.value("format", "auto");
		args_.output_format = raw_json.value("output_format", "text");
		args_.output_file = raw_json.value("output_file", "");
		args_.offset = raw_json.value("offset", 0);
		args_.length = raw_json.value("length", -1);
		
		if (args_.input_data.empty()) {
			out_error = "input_data cannot be empty.";
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
