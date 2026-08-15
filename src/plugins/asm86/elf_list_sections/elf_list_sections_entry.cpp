#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "fs_utils.h"
#include "hex/elf.h"
#include "elf_list_sections.h"

namespace tools
{

elf_list_sections_tool::elf_list_sections_tool(elf_list_sections_args args)
    : llm_tool_action("Listing ELF sections in " + args.requested_path), args_(std::move(args))
{
}

bool elf_list_sections_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
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

std::string elf_list_sections_tool::execute(agentlib::tool_context &ctx)
{
	if (!fs_utils::is_regular_file(args_.safe_path)) {
		set_failure(ctx, "Target path is not a regular file.");
		return "Error: Target path is not a regular file: " + args_.requested_path;
	}

	std::ifstream file(args_.safe_path, std::ios::binary);
	if (!file.is_open()) {
		set_failure(ctx, "Failed to open file.");
		return "Error: Failed to open file for reading.";
	}

	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	if (size > 52428800) {
		file.close();
		set_failure(ctx, "File exceeds 50MB size limit.");
		return "Error: File exceeds maximum allowed size of 50MB.";
	}

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

	std::string safe_req_path = escape_markdown_table_str(args_.requested_path);
	std::string result = std::format("### ELF Section Headers: {}\n\n"
					 "| Index | Name | Offset | Size | Semantic |\n"
					 "| --- | --- | --- | --- | --- |\n",
					 safe_req_path);

	const auto &sections = parser.get_sections();
	for (const auto &sec : sections) {
		std::string sem_desc = "Normal";
		if (sec.semantic == hex_semantic_type::code_section) {
			sem_desc = "Code (.text)";
		} else if (sec.semantic == hex_semantic_type::data_section) {
			sem_desc = "Data";
		}
		std::string safe_sec_name = escape_markdown_table_str(sec.name);
		result += std::format("| {} | {} | 0x{:X} | 0x{:X} | {} |\n", sec.index, safe_sec_name, sec.offset, sec.size, sem_desc);
	}

	set_success(ctx, "Listed " + std::to_string(sections.size()) + " sections.");
	return fs_utils::wrap_prompt_untrusted_data_tag("elf_sections_result", result);
}

} // namespace tools
