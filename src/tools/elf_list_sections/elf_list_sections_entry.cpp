#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#include "../../ui/hex_highlighter.h"
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

std::string elf_list_sections_tool::execute(agentlib::tool_context &ctx)
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

	std::string result = std::format("### ELF Section Headers: {}\n\n"
					 "| Index | Name | Offset | Size | Semantic |\n"
					 "| --- | --- | --- | --- | --- |\n",
					 args_.requested_path);

	const auto &sections = parser.get_sections();
	for (const auto &sec : sections) {
		std::string sem_desc = "Normal";
		if (sec.semantic == hex_semantic_type::code_section) {
			sem_desc = "Code (.text)";
		} else if (sec.semantic == hex_semantic_type::data_section) {
			sem_desc = "Data";
		}
		result += std::format("| {} | {} | 0x{:X} | 0x{:X} | {} |\n", sec.index, sec.name, sec.offset, sec.size, sem_desc);
	}

	set_success(ctx, "Listed " + std::to_string(sections.size()) + " sections.");
	return result;
}

} // namespace tools
