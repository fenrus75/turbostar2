#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools
{

struct elf_list_symbols_args {
	std::string requested_path;
	std::string safe_path;
	std::string pattern;
};

class elf_list_symbols_tool : public agentlib::llm_tool_action
{
      public:
	explicit elf_list_symbols_tool(elf_list_symbols_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	elf_list_symbols_args args_;
};

} // namespace tools
