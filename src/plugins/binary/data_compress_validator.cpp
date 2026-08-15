#include "data_compress_validator.h"
#include "agentlib/tool_registry.h"

namespace tools {

std::string data_compress_validator::get_description() const {
	return "Compress data into various formats. Accepts raw data strings (input_data) or a target file path (path).";
}

nlohmann::json data_compress_validator::get_parameters_schema() const {
	return {
	    {"type", "object"},
	    {"properties",
	     {{"path", {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). Input file to compress. Mutually exclusive with 'input_data'."}}},
	      {"input_data", {{"type", "string"}, {"description", "Raw data string, hex, base64, or data URL to compress. Mutually exclusive with 'path'."}}},
	      {"format", {{"type", "string"}, {"description", "Format to use. 'deflate' is an alias for 'zlib'."}, {"enum", {"zstd", "gzip", "zlib", "deflate", "xz", "bzip2", "lz4"}}, {"default", "zstd"}}},
	      {"output_format", {{"type", "string"}, {"description", "Return format."}, {"enum", {"hex", "base64", "text"}}, {"default", "hex"}}},
	      {"output_path", {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.bin'). Optional file path to write output instead of returning it."}}}}},
	    {"required", nlohmann::json::array()}};
}

bool data_compress_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const {
	try {
		args_.input_data = raw_json.value("input_data", "");
		args_.path = raw_json.value("path", "");
		args_.format = raw_json.value("format", "zstd");
		args_.output_format = raw_json.value("output_format", "hex");
		args_.output_path = raw_json.value("output_path", "");
		
		bool has_data = !args_.input_data.empty();
		bool has_file = !args_.path.empty();

		if (!has_data && !has_file) {
			out_error = "Must specify exactly one of 'path' or 'input_data'.";
			return false;
		}
		if (has_data && has_file) {
			out_error = "Parameters 'input_data' and 'path' are mutually exclusive.";
			return false;
		}

		if (has_file) {
			if (args_.path.find("://") == std::string::npos) {
				std::string canonical_path;
				if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, canonical_path, out_error)) {
					return false;
				}
				args_.safe_path = canonical_path;
			} else {
				args_.safe_path = args_.path;
			}
		}

		if (!args_.output_path.empty()) {
			if (args_.output_path.find("://") == std::string::npos) {
				std::string canonical_output;
				if (!ctx.fs_security.validate_access(args_.output_path, agentlib::access_type::write, canonical_output, out_error)) {
					return false;
				}
				args_.safe_output_path = canonical_output;
			} else {
				args_.safe_output_path = args_.output_path;
			}
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
