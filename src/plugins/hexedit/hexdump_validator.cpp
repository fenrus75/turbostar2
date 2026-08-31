#include "plugins/hexedit/hexdump_validator.h"
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"

namespace tools
{

struct hexdump_raw_args {
	std::string path;
	size_t offset{0};
	size_t size{0};
	std::string offset_by_name;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(hexdump_raw_args, path, offset, size, offset_by_name);

std::string hexdump_validator::get_description() const
{
	return "Reads a range of bytes from a binary or text file and returns a formatted hexdump "
	       "complete with offset offsets, raw hex tuples, ASCII representations, and structural annotations "
	       "(such as ELF/PNG/JPEG headers) if format-specific patterns are detected.";
}

nlohmann::json hexdump_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"path", {{"type", "string"}, {"description", "The path to the file relative to the project root."}}},
	      {"offset", {{"type", nlohmann::json::array({"integer", "string"})}, {"description", "Byte offset from which to start reading (e.g. 0 or \"0x1080\"). Defaults to 0."}}},
	      {"size", {{"type", nlohmann::json::array({"integer", "string"})}, {"description", "Number of bytes to read."}}},
	      {"offset_by_name", {{"type", "string"}, {"description", "Optional section/chunk/symbol name (e.g. '.text' or 'PLTE') to resolve offset automatically."}}}}},
	    {"required", nlohmann::json::array({"path", "size"})}};
}

bool hexdump_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
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
		args_.size = parse_numeric(raw_json, "size", 0);
		args_.offset_by_name = raw_json.value("offset_by_name", "");

		if (args_.requested_path.empty()) {
			out_error = "Path cannot be empty.";
			return false;
		}
		if (args_.size == 0) {
			out_error = "Size must be at least 1 byte.";
			return false;
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(args_.requested_path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.safe_path = canonical_path;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> hexdump_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<hexdump_tool>(args_);
}

} // namespace tools

extern "C" {
void register_hexdump(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::hexdump_validator>(); });
}

void unregister_hexdump(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("hexdump");
}
}
