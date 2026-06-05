#include <nlohmann/json.hpp>
#include <string>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "x86_disassemble.h"

namespace tools
{

struct x86_disassemble_raw_args {
	std::string data;
	std::string format = "auto";
	std::string mode = "64";
	std::string syntax = "intel";
	uint64_t address = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(x86_disassemble_raw_args, data, format, mode, syntax, address);

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
		return "Disassembles raw x86/x64 machine code bytes into human-readable assembly instructions. "
		       "Supports Intel and AT&T syntax, and 16-bit, 32-bit, or 64-bit modes.";
	}

	std::string get_family() const override
	{
		return "x86";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"data",
			   {{"type", "string"},
			    {"description", "The raw machine code bytes to disassemble. Can be Base64 or space-separated hex."}}},
			  {"format",
			   {{"type", "string"},
			    {"enum", nlohmann::json::array({"hex", "base64", "auto"})},
			    {"description", "The format of the input data. Defaults to 'auto'."}}},
			  {"mode",
			   {{"type", "string"},
			    {"enum", nlohmann::json::array({"16", "32", "64"})},
			    {"description", "The CPU mode (16-bit, 32-bit, or 64-bit). Defaults to '64'."}}},
			  {"syntax",
			   {{"type", "string"},
			    {"enum", nlohmann::json::array({"intel", "att"})},
			    {"description", "Assembly syntax format (intel or att). Defaults to 'intel'."}}},
			  {"address", {{"type", "integer"}, {"description", "The starting runtime address/IP offset. Defaults to 0."}}}}},
			{"required", nlohmann::json::array({"data"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			x86_disassemble_raw_args parsed = raw_json.get<x86_disassemble_raw_args>();
			if (parsed.data.empty()) {
				out_error = "Input data cannot be empty.";
				return false;
			}

			args_.data = parsed.data;
			args_.format = parsed.format;
			args_.mode = parsed.mode;
			args_.syntax = parsed.syntax;
			args_.address = parsed.address;

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

REGISTER_TOOL(x86_disassemble_validator)

} // namespace tools
