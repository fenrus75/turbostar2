#include "data_compress_validator.h"
#include "agentlib/tool_registry.h"

namespace tools {

std::string data_compress_validator::get_description() const {
	return "Compress data into various formats. Supports file paths, data URLs, hex strings, or base64 as input.";
}

nlohmann::json data_compress_validator::get_parameters_schema() const {
	return {
	    {"type", "object"},
	    {"properties",
	     {{"input_data", {{"type", "string"}, {"description", "The input data to compress."}}},
	      {"format", {{"type", "string"}, {"description", "Format to use. 'deflate' is an alias for 'zlib'."}, {"enum", {"zstd", "gzip", "zlib", "deflate", "xz", "bzip2", "lz4"}}, {"default", "zstd"}}},
	      {"output_format", {{"type", "string"}, {"description", "Return format."}, {"enum", {"hex", "base64", "text"}}, {"default", "hex"}}},
	      {"output_file", {{"type", "string"}, {"description", "Optional file to write output."}}}}},
	    {"required", nlohmann::json::array({"input_data"})}};
}

bool data_compress_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const {
	try {
		args_.input_data = raw_json.value("input_data", "");
		args_.format = raw_json.value("format", "zstd");
		args_.output_format = raw_json.value("output_format", "hex");
		args_.output_file = raw_json.value("output_file", "");
		
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

std::unique_ptr<agentlib::llm_tool> data_compress_validator::create_tool_impl(const nlohmann::json &) const {
	return std::make_unique<data_compress_tool>(args_);
}

}

extern "C" {
void register_data_compress(void) {
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::data_compress_validator>(); });
}
void unregister_data_compress(void) {
	agentlib::tool_registry::get_instance().unregister_validator("data_compress");
}
}
