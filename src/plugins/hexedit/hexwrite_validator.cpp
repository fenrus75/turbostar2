#include "plugins/hexedit/hexwrite_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"

namespace tools
{

struct hexwrite_raw_args {
	std::string path;
	size_t start_offset{0};
	std::string data;
	std::string offset_by_name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(hexwrite_raw_args, path, start_offset, data, offset_by_name);

std::string hexwrite_validator::get_description() const
{
	return "Writes hexadecimal bytes to a binary or text file at a specified offset (overwrite mode). "
	       "Creates the file if it does not exist. Auto-grows the file with 0x00 padding if writing past EOF. "
	       "Hex input is robust and ignores common formatting delimiters (spaces, commas, brackets, prefixes like 0x).";
}

nlohmann::json hexwrite_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"path", {{"type", "string"}, {"description", "The path to the file relative to the project root."}}},
	      {"start_offset", {{"type", "integer"}, {"minimum", 0}, {"description", "Byte offset at which to overwrite. Defaults to 0."}}},
	      {"data", {{"type", "string"}, {"description", "Hexadecimal data string to write (e.g. '7f 45 4c 46' or '7f454c46' or '0x7f, 0x45')."}}},
	      {"offset_by_name", {{"type", "string"}, {"description", "Optional section/chunk/symbol name (e.g. '.text' or 'PLTE') to resolve start_offset automatically."}}}}},
	    {"required", nlohmann::json::array({"path", "data"})}};
}

bool hexwrite_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
					    std::string &out_error) const
{
	try {
		hexwrite_raw_args parsed = raw_json.get<hexwrite_raw_args>();
		if (parsed.path.empty()) {
			out_error = "Path cannot be empty.";
			return false;
		}
		if (parsed.data.empty()) {
			out_error = "Data cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		args_.requested_path = parsed.path;
		args_.safe_path = canonical_path;
		args_.start_offset = parsed.start_offset;
		args_.hex_data = parsed.data;
		args_.offset_by_name = parsed.offset_by_name;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> hexwrite_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<hexwrite_tool>(args_);
}

} // namespace tools

extern "C" {
void register_hexwrite(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::hexwrite_validator>(); });
}

void unregister_hexwrite(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("hexwrite");
}
}
