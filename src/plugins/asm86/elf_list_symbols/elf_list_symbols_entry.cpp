#include <filesystem>
#include <format>
#include <fstream>
#include <re2/re2.h>
#include <vector>
#include "fs_utils.h"
#include "hex/elf.h"
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

static std::string escape_markdown_table_str(const std::string &untrusted_str)
{
	std::string out;
	out.reserve(untrusted_str.size());
	for (char c : untrusted_str) {
		if (c == '|') out += "\\|";
		else if (c == '\n' || c == '\r') out += " ";
		else if (c == '\\') out += "\\\\";
		else if (static_cast<unsigned char>(c) >= 32 && c != 127) out += c;
	}
	return out;
}

std::string elf_list_symbols_tool::execute(agentlib::tool_context &ctx)
{
	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file.");
		return "Error: Failed to open file for reading.";
	}

	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> bytes(size);
	if (size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()), size)) {
		file.close();
		set_failure(ctx, "Failed to read file.");
		return "Error: Failed to read file.";
	}
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

	std::string safe_req_path = escape_markdown_table_str(args_.requested_path);
	std::string result = std::format("### ELF Symbol Table: {}\n\n"
					 "| Name | Offset/Value | Size |\n"
					 "| --- | --- | --- |\n",
					 safe_req_path);

	const auto &symbols = parser.get_symbols();
	size_t count = 0;
	for (const auto &sym : symbols) {
		if (filter_re && !re2::RE2::PartialMatch(sym.name, *filter_re)) {
			continue;
		}
		std::string safe_sym_name = escape_markdown_table_str(sym.name);
		result += std::format("| {} | 0x{:X} | 0x{:X} |\n", safe_sym_name, sym.offset, sym.size);
		count++;
		if (count >= 200) {
			result += "\n*(Remaining symbols omitted to save context. Refine your query pattern to find specific symbols.)*\n";
			break;
		}
	}

	set_success(ctx, "Listed " + std::to_string(count) + " symbols.");
	return fs_utils::wrap_prompt_untrusted_data_tag("elf_symbols_result", result);
}

} // namespace tools
