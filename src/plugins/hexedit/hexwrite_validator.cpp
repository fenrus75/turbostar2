#include "plugins/hexedit/hexwrite_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"

namespace tools
{

struct hexwrite_raw_args {
	std::string path;
	size_t offset{0};
	std::string data;
	std::string offset_by_name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(hexwrite_raw_args, path, offset, data, offset_by_name);

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
	      {"offset", {{"type", nlohmann::json::array({"integer", "string"})}, {"description", "Byte offset at which to overwrite (e.g. 0 or \"0x1080\"). Defaults to 0."}}},
	      {"data", {{"type", "string"}, {"description", "Hexadecimal data string to write (e.g. '7f 45 4c 46' or '7f454c46' or '0x7f, 0x45')."}}},
	      {"offset_by_name", {{"type", "string"}, {"description", "Optional section/chunk/symbol name (e.g. '.text' or 'PLTE') to resolve offset automatically."}}}}},
	    {"required", nlohmann::json::array({"path", "data"})}};
}

bool hexwrite_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
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
		args_.hex_data = raw_json.value("data", "");
		args_.offset_by_name = raw_json.value("offset_by_name", "");

		if (args_.requested_path.empty()) {
			out_error = "Path cannot be empty.";
			return false;
		}
		if (args_.hex_data.empty()) {
			out_error = "Data cannot be empty.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(args_.requested_path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		args_.safe_path = canonical_path;

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
