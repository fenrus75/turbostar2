#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include <re2/re2.h>
#include "../../ui/hex_highlighter.h"
#include "elf_list_symbols.h"

namespace tools
{

elf_list_symbols_tool::elf_list_symbols_tool(elf_list_symbols_args args)
    : llm_tool_action("Listing ELF symbols in " + args.requested_path), args_(std::move(args))
{
}

bool elf_list_symbols_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string elf_list_symbols_tool::execute(agentlib::tool_context &ctx)
{
	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file.");
		return "Error: Failed to open file for reading.";
	}

	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	elf_hex_highlighter parser;
	if (!parser.can_handle(bytes)) {
		set_failure(ctx, "File is not a valid ELF file.");
		return "Error: File is not a valid ELF file.";
	}

	if (!parser.parse(bytes)) {
		set_failure(ctx, "Failed to parse ELF headers.");
		return "Error: Failed to parse ELF headers.";
	}

	// Setup regex if pattern is provided
	std::unique_ptr<re2::RE2> filter_re;
	if (!args_.pattern.empty()) {
		re2::RE2::Options options;
		options.set_case_sensitive(false);
		filter_re = std::make_unique<re2::RE2>(args_.pattern, options);
		if (!filter_re->ok()) {
			set_failure(ctx, "Invalid regular expression pattern.");
			return "Error: Invalid regular expression pattern.";
		}
	}

	std::string result = std::format("### ELF Symbol Table: {}\n\n"
					 "| Name | Offset/Value | Size |\n"
					 "| --- | --- | --- |\n",
					 args_.requested_path);

	const auto &symbols = parser.get_symbols();
	size_t count = 0;
	for (const auto &sym : symbols) {
		if (filter_re && !re2::RE2::PartialMatch(sym.name, *filter_re)) {
			continue;
		}
		result += std::format("| {} | 0x{:X} | 0x{:X} |\n", sym.name, sym.offset, sym.size);
		count++;
		if (count >= 200) {
			result += "\n*(Remaining symbols omitted to save context. Refine your query pattern to find specific symbols.)*\n";
			break;
		}
	}

	set_success(ctx, "Listed " + std::to_string(count) + " symbols.");
	return result;
}

} // namespace tools
