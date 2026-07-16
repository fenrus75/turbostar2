#include "image_import.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "fs_utils.h"

namespace tools
{

nlohmann::json image_import_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"filename", {{"type", "string"}, {"description", "Optional. Path to a local image file to import. Must be relative to the project root (e.g. 'logo.jpg')."}}},
	      {"URL", {{"type", "string"}, {"description", "Optional. HTTP/HTTPS URL of the remote image to download and import."}}},
	      {"output", {{"type", "string"}, {"description", "A friendly alias name to assign to the imported image in the VFS (e.g. 'logo'). Subsequent tools can reference the image using this alias."}}}}},
	    {"required", nlohmann::json::array({"output"})}};
}

bool image_import_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						    std::string &out_error) const
{
	try {
		if (!raw_json.contains("output") || !raw_json["output"].is_string()) {
			out_error = "Missing or invalid 'output' argument.";
			return false;
		}
		std::string output = raw_json["output"].get<std::string>();
		if (output.empty()) {
			out_error = "The output alias name cannot be empty.";
			return false;
		}

		bool has_filename = raw_json.contains("filename") && raw_json["filename"].is_string();
		bool has_url = raw_json.contains("URL") && raw_json["URL"].is_string();

		if (!has_filename && !has_url) {
			out_error = "Must specify either filename or URL.";
			return false;
		}
		if (has_filename && has_url) {
			out_error = "Cannot specify both filename and URL. Choose one.";
			return false;
		}

		parsed_args_.filename = std::nullopt;
		parsed_args_.URL = std::nullopt;

		if (has_filename) {
			std::string filename = raw_json["filename"].get<std::string>();
			if (filename.empty()) {
				out_error = "filename cannot be empty.";
				return false;
			}
			std::string resolved;
			if (!ctx.fs_security.validate_access(filename, agentlib::access_type::read, resolved, out_error)) {
				return false;
			}
			if (!std::filesystem::exists(resolved)) {
				out_error = "Local file does not exist: " + filename;
				return false;
			}
			parsed_args_.filename = resolved;
		}

		if (has_url) {
			std::string url = raw_json["URL"].get<std::string>();
			if (url.empty()) {
				out_error = "URL cannot be empty.";
				return false;
			}
			if (!url.starts_with("http://") && !url.starts_with("https://")) {
				out_error = "URL must start with http:// or https://";
				return false;
			}
			parsed_args_.URL = url;
		}

		parsed_args_.output = output;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_import_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_import_tool>(parsed_args_);
}

} // namespace tools

extern "C" {
void register_image_import(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::image_import_validator>(); });
}

void unregister_image_import(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("image_import");
}
}
