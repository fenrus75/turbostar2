#include "plugins/hexedit/hexinspect_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"

namespace tools
{

struct hexinspect_raw_args {
	std::string path;
	size_t offset{0};
	size_t size{256};
	std::string offset_by_name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(hexinspect_raw_args, path, offset, size, offset_by_name);

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
	      {"offset", {{"type", nlohmann::json::array({"integer", "string"})}, {"description", "0-based byte offset to start inspecting (e.g. 0 or \"0x1080\"). Defaults to 0."}}},
	      {"size", {{"type", nlohmann::json::array({"integer", "string"})}, {"description", "Number of bytes to inspect. Defaults to 256. Maximum 4096."}}},
	      {"offset_by_name", {{"type", "string"}, {"description", "Optional section/chunk/symbol name (e.g. '.text' or 'PLTE') to resolve offset automatically."}}}}},
	    {"required", nlohmann::json::array({"path"})}};
}

bool hexinspect_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
					      std::string &out_error) const
{
	try {
		auto parse_numeric = [](const nlohmann::json &j, const char *key, size_t def_val) -> size_t {
			if (!j.contains(key)) return def_val;
			auto &v = j[key];
			if (v.is_number()) return v.get<size_t>();
			if (v.is_string()) {
				std::string s = v.get<std::string>();
				return s.starts_with("0x") || s.starts_with("0X") ? std::stoull(s.substr(2), nullptr, 16) : std::stoull(s);
			}
			return def_val;
		};

		args_.requested_path = raw_json.value("path", "");
		args_.offset = parse_numeric(raw_json, "offset", 0);
		size_t parsed_size = parse_numeric(raw_json, "size", 0);
		args_.offset_by_name = raw_json.value("offset_by_name", "");

		if (args_.requested_path.empty()) {
			out_error = "Path parameter cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(args_.requested_path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.safe_path = canonical_path;
		args_.size = (parsed_size == 0) ? 256 : (parsed_size > 4096 ? 4096 : parsed_size);

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
