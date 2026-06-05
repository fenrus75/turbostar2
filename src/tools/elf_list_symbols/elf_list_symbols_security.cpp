#include <nlohmann/json.hpp>
#include <string>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "elf_list_symbols.h"

namespace tools
{

struct elf_list_symbols_raw_args {
	std::string path;
	std::string pattern = "";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(elf_list_symbols_raw_args, path, pattern);

class elf_list_symbols_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "elf_list_symbols";
	}

	std::string get_description() const override
	{
		return "Lists all symbols in the symbol table of an ELF file, providing their name, offset/value, and size. "
		       "Allows optional case-insensitive substring or regex filtering via the 'pattern' argument.";
	}

	std::string get_family() const override
	{
		return "x86";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
			{"type", "object"},
			{"properties", {
				{"path", {{"type", "string"}, {"description", "The path to the ELF file relative to project root."}}},
				{"pattern", {{"type", "string"}, {"description", "Optional substring or regex pattern to filter symbol names."}}}
			}},
			{"required", nlohmann::json::array({"path"})}
		};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override
	{
		try {
			elf_list_symbols_raw_args parsed = raw_json.get<elf_list_symbols_raw_args>();
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
			args_.pattern = parsed.pattern;

			return true;
		} catch (const std::exception &e) {
			out_error = "Invalid arguments: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<elf_list_symbols_tool>(args_);
	}

      private:
	mutable elf_list_symbols_args args_;
};

REGISTER_TOOL(elf_list_symbols_validator)

} // namespace tools
