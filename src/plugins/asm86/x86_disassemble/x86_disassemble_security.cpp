#include <nlohmann/json.hpp>
#include <string>
#include "agentlib/json_utils.h"
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "fs_utils.h"
#include "x86_disassemble.h"

namespace tools
{

class x86_disassemble_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "x86_disassemble";
	}

	std::string get_description() const override
	{
		return "Disassembles x86/x64 machine code from either an ELF/binary file ('path' with optional 'symbol', 'offset', 'size') "
		       "or raw bytes ('data' in hex or Base64). Automatically resolves ELF call targets, PLT stubs, global objects, and string literals.";
	}

	std::string get_family() const override
	{
		return "x86";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"path",
			   {{"type", "string"},
			    {"description", "Optional path to an ELF or binary file relative to the project root."}}},
			  {"symbol",
			   {{"type", "string"},
			    {"description", "Optional function symbol or section name (e.g. 'main' or '.text') when 'path' is provided. Automatically resolves offset and size from the symbol table."}}},
			  {"offset",
			   {{"type", nlohmann::json::array({"integer", "string"})},
			    {"description", "Optional starting byte offset (or virtual address) when 'path' is provided (e.g. 0 or '0x114D8')."}}},
			  {"size",
			   {{"type", nlohmann::json::array({"integer", "string"})},
			    {"description", "Optional number of bytes to disassemble when 'path' is provided. Defaults to the symbol size (or 256 bytes, max 4096)."}}},
			  {"data",
			   {{"type", "string"},
			    {"description", "Raw machine code bytes to disassemble (Base64 or hex) when 'path' is not used."}}},
			  {"format",
			   {{"type", "string"},
			    {"enum", nlohmann::json::array({"hex", "base64", "auto"})},
			    {"description", "The format of 'data'. Defaults to 'auto'."}}},
			  {"mode",
			   {{"type", "string"},
			    {"enum", nlohmann::json::array({"16", "32", "64"})},
			    {"description", "The CPU mode (16-bit, 32-bit, or 64-bit). Defaults to '64' (or auto-detected from ELF header)."}}},
			  {"syntax",
			   {{"type", "string"},
			    {"enum", nlohmann::json::array({"att", "intel"})},
			    {"description", "Assembly syntax format (att or intel). Defaults to 'att'."}}},
			  {"address",
			   {{"type", nlohmann::json::array({"integer", "string"})},
			    {"description", "The starting runtime address/IP offset for raw 'data'. Defaults to 0."}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			args_ = x86_disassemble_args{};
			args_.data = raw_json.value("data", "");
			args_.requested_path = raw_json.value("path", "");
			args_.symbol = raw_json.value("symbol", "");
			if (args_.symbol.empty()) {
				args_.symbol = raw_json.value("offset_by_name", "");
			}
			args_.format = raw_json.value("format", "auto");
			args_.mode = raw_json.value("mode", "64");
			args_.mode_specified = raw_json.contains("mode");
			args_.syntax = raw_json.value("syntax", "att");
			args_.address = json_utils::parse_numeric_from_json(raw_json, "address", 0);
			args_.offset = json_utils::parse_numeric_from_json(raw_json, "offset", 0);
			size_t parsed_size = json_utils::parse_numeric_from_json(raw_json, "size", 0);
			args_.size = (parsed_size > 4096) ? 4096 : parsed_size;

			if (args_.requested_path.empty() && args_.data.empty()) {
				out_error = "Either 'path' or 'data' must be provided.";
				return false;
			}

			if (!args_.symbol.empty() && !fs_utils::is_safe_for_ui(args_.symbol)) {
				out_error = "Security Violation: symbol contains unsafe control characters.";
				return false;
			}

			if (!args_.requested_path.empty()) {
				std::string canonical_path;
				if (!ctx.fs_security.validate_access(args_.requested_path, agentlib::access_type::read, canonical_path, out_error)) {
					return false;
				}
				args_.safe_path = canonical_path;
			}

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<x86_disassemble_tool>(args_);
	}

      private:
	mutable x86_disassemble_args args_;
};

} // namespace tools

extern "C" {
void register_x86_disassemble(void)
{
	agentlib::tool_registry::get_instance().register_validator([]() { return std::make_unique<tools::x86_disassemble_validator>(); });
}

void unregister_x86_disassemble(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("x86_disassemble");
}
}
