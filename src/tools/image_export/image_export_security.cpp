#include "image_export.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "images/image_manager.h"

namespace tools
{

nlohmann::json image_export_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"name", {{"type", "string"}, {"description", "The developer-assigned name alias (e.g. 'logo') or the full VFS URI (e.g. 'images://by-sha256/<hash>') of the image to export."}}},
	      {"filename", {{"type", "string"}, {"description", "The destination path relative to the project root (e.g. 'output/logo_gray.png') where the image file will be saved. Overwriting the original input file is permitted."}}}}},
	    {"required", nlohmann::json::array({"name", "filename"})}};
}

bool image_export_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						    std::string &out_error) const
{
	try {
		if (!raw_json.contains("name") || !raw_json["name"].is_string()) {
			out_error = "Missing or invalid 'name' argument.";
			return false;
		}
		if (!raw_json.contains("filename") || !raw_json["filename"].is_string()) {
			out_error = "Missing or invalid 'filename' argument.";
			return false;
		}

		std::string name = raw_json["name"].get<std::string>();
		std::string filename = raw_json["filename"].get<std::string>();

		if (name.empty()) {
			out_error = "name cannot be empty.";
			return false;
		}
		if (filename.empty()) {
			out_error = "filename cannot be empty.";
			return false;
		}

		auto resolve_image_uri = [](const std::string &uri) -> std::string {
			if (uri.starts_with("images://")) {
				return images::image_manager::get_instance().resolve_uri(uri);
			}
			std::string resolved = images::image_manager::get_instance().resolve_uri("images://by-name/" + uri);
			if (!resolved.empty()) return resolved;
			return images::image_manager::get_instance().resolve_uri("images://" + uri);
		};

		std::string resolved_src = resolve_image_uri(name);
		if (resolved_src.empty()) {
			out_error = "Source VFS image could not be resolved: " + name;
			return false;
		}

		std::string resolved_dest;
		if (!ctx.fs_security.validate_access(filename, agentlib::access_type::write, resolved_dest, out_error)) {
			return false;
		}

		parsed_args_.name = name;
		parsed_args_.safe_path = resolved_dest;
		parsed_args_.original_filename = filename;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> image_export_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<image_export_tool>(parsed_args_);
}

REGISTER_TOOL(image_export_validator)

} // namespace tools
