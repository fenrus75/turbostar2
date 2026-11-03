#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "address_lookup.h"
#include "fs_utils.h"
#include "hex/elf.h"
#include "pcre2_regex.h"
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
	std::unique_ptr<pcre2_regex> filter_re;
	if (!args_.pattern.empty()) {
		pcre2_regex::compile_options opts;
		opts.caseless = true;
		filter_re = std::make_unique<pcre2_regex>(args_.pattern, opts);
		if (!filter_re->is_valid()) {
			set_failure(ctx, "Invalid regular expression pattern.");
			return "Error: Invalid regular expression pattern.";
		}
	}

	const auto &symbols = parser.get_symbols();
	std::vector<const elf_hex_highlighter::parsed_symbol *> matched_symbols;
	bool truncated = false;
	for (const auto &sym : symbols) {
		if (filter_re && !filter_re->partial_match(sym.name)) {
			continue;
		}
		if (matched_symbols.size() >= 200) {
			truncated = true;
			break;
		}
		matched_symbols.push_back(&sym);
	}

	std::vector<uintptr_t> vaddrs;
	vaddrs.reserve(matched_symbols.size());
	for (const auto *sym : matched_symbols) {
		vaddrs.push_back(static_cast<uintptr_t>(sym->vaddr));
	}
	auto resolved_locs = turbostar::address_lookup::resolve_addresses(vaddrs, args_.safe_path);

	std::string safe_req_path = escape_markdown_table_str(args_.requested_path);
	std::string result = std::format("### ELF Symbol Table: {}\n\n"
					 "| Name | Offset/Value | Size | Source |\n"
					 "| --- | --- | --- | --- |\n",
					 safe_req_path);

	for (size_t i = 0; i < matched_symbols.size(); ++i) {
		const auto *sym = matched_symbols[i];
		std::string safe_sym_name = escape_markdown_table_str(sym->name);
		std::string source_loc = "-";
		if (i < resolved_locs.size() && !resolved_locs[i].file_path.empty() &&
		    resolved_locs[i].file_path != "??" && resolved_locs[i].line_number > 0) {
			source_loc = escape_markdown_table_str(
			    std::format("{}:{}", resolved_locs[i].file_path, resolved_locs[i].line_number));
		}
		result += std::format("| {} | 0x{:X} | 0x{:X} | {} |\n", safe_sym_name, sym->offset, sym->size, source_loc);
	}
	if (truncated) {
		result += "\n*(Remaining symbols omitted to save context. Refine your query pattern to find specific symbols.)*\n";
	}

	set_success(ctx, "Listed " + std::to_string(matched_symbols.size()) + " symbols.");
	return fs_utils::wrap_prompt_untrusted_data_tag("elf_symbols_result", result);
}

} // namespace tools
