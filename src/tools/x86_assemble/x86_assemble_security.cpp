#include <nlohmann/json.hpp>
#include <string>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "x86_assemble.h"

namespace tools
{

struct x86_assemble_raw_args {
	std::string instruction;
	std::string mode = "64";
	std::string syntax = "intel";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(x86_assemble_raw_args, instruction, mode, syntax);

class x86_assemble_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "x86_assemble";
	}

	std::string get_description() const override
	{
		return "Assembles a single x86/x64 assembly instruction string into its corresponding machine code bytes (space-separated "
		       "hex). "
		       "Supports Intel and AT&T syntax, and 16-bit, 32-bit, or 64-bit modes.";
	}

	std::string get_family() const override
	{
		return "x86";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"instruction", {{"type", "string"}, {"description", "The assembly instruction to assemble (e.g. 'mov eax, eax')."}}},
		      {"mode",
		       {{"type", "string"},
			{"enum", nlohmann::json::array({"16", "32", "64"})},
			{"description", "The CPU mode (16-bit, 32-bit, or 64-bit). Defaults to '64'."}}},
		      {"syntax",
		       {{"type", "string"},
			{"enum", nlohmann::json::array({"intel", "att"})},
			{"description", "Assembly syntax format (intel or att). Defaults to 'intel'."}}}}},
		    {"required", nlohmann::json::array({"instruction"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			x86_assemble_raw_args parsed = raw_json.get<x86_assemble_raw_args>();
			if (parsed.instruction.empty()) {
				out_error = "Instruction cannot be empty.";
				return false;
			}

			args_.instruction = parsed.instruction;
			args_.mode = parsed.mode;
			args_.syntax = parsed.syntax;

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<x86_assemble_tool>(args_);
	}

      private:
	mutable x86_assemble_args args_;
};

REGISTER_TOOL(x86_assemble_validator)

} // namespace tools
