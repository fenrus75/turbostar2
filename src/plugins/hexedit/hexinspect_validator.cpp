#include "plugins/hexedit/hexinspect_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"

namespace tools
{

struct hexinspect_raw_args {
	std::string path;
	size_t start_offset{0};
	size_t size{256};
	std::string offset_by_name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(hexinspect_raw_args, path, start_offset, size, offset_by_name);

std::string hexinspect_validator::get_description() const
{
	return "Inspects the semantic/structural details of a binary file range using registered syntax highlighters (like ELF, PNG, or JPEG). "
	       "Returns a markdown list of fields, offsets, sizes, and annotations in the range.";
}

nlohmann::json hexinspect_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"path", {{"type", "string"}, {"description", "The path to the binary file relative to project root."}}},
	      {"start_offset", {{"type", "integer"}, {"description", "0-based byte offset to start inspecting. Defaults to 0."}}},
	      {"size", {{"type", "integer"}, {"description", "Number of bytes to inspect. Defaults to 256. Maximum 4096."}}},
	      {"offset_by_name", {{"type", "string"}, {"description", "Optional section/chunk/symbol name (e.g. '.text' or 'PLTE') to resolve start_offset automatically."}}}}},
	    {"required", nlohmann::json::array({"path"})}};
}

bool hexinspect_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
					      std::string &out_error) const
{
	try {
		hexinspect_raw_args parsed = raw_json.get<hexinspect_raw_args>();
		if (parsed.path.empty()) {
			out_error = "Path parameter cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.requested_path = parsed.path;
		args_.safe_path = canonical_path;
		args_.start_offset = parsed.start_offset;
		args_.size = (parsed.size == 0) ? 256 : (parsed.size > 4096 ? 4096 : parsed.size);
		args_.offset_by_name = parsed.offset_by_name;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> hexinspect_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<hexinspect_tool>(args_);
}

} // namespace tools

extern "C" {
void register_hexinspect(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::hexinspect_validator>(); });
}

void unregister_hexinspect(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("hexinspect");
}
}
