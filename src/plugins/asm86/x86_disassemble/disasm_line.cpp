#include "plugins/asm86/x86_disassemble/disasm_line.h"
#include <algorithm>
#include <format>
#include <map>
#include "hex/elf.h"

#ifdef HAVE_ZYDIS
#include <Zydis/Zydis.h>
#endif

namespace tools
{


disasm_line disasm_line::decode_one(std::span<const uint8_t> buffer,
                                    size_t offset,
                                    uint64_t runtime_addr,
                                    std::string_view mode,
                                    std::string_view syntax,
                                    bool is_64_bit_addr)
{
	disasm_line line;
	line.address = runtime_addr;
	line.file_offset = offset;

	if (offset >= buffer.size()) {
		line.type = disasm_line_type::data_byte;
		return line;
	}

#ifdef HAVE_ZYDIS
	ZydisMachineMode machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
	if (mode == "32") {
		machine_mode = ZYDIS_MACHINE_MODE_LEGACY_32;
	} else if (mode == "16") {
		machine_mode = ZYDIS_MACHINE_MODE_LEGACY_16;
	}

	ZydisDisassembledInstruction instruction;
	ZyanStatus status;
	if (syntax == "intel") {
		status = ZydisDisassembleIntel(machine_mode, runtime_addr, buffer.data() + offset,
		                               buffer.size() - offset, &instruction);
	} else {
		status = ZydisDisassembleATT(machine_mode, runtime_addr, buffer.data() + offset,
		                             buffer.size() - offset, &instruction);
	}

	if (ZYAN_SUCCESS(status) && instruction.info.length > 0) {
		line.type = disasm_line_type::instruction;
		line.length = std::min<size_t>(instruction.info.length, buffer.size() - offset);
		size_t copy_len = std::min<size_t>(line.length, line.bytes.size());
		std::copy_n(buffer.data() + offset, copy_len, line.bytes.begin());
		line.text = elf_hex_highlighter::shorten_hex(instruction.text);

		auto cat = instruction.info.meta.category;
		auto mnem = instruction.info.mnemonic;
		line.is_call = (cat == ZYDIS_CATEGORY_CALL);
		line.is_conditional = (cat == ZYDIS_CATEGORY_COND_BR);
		line.is_branch = (cat == ZYDIS_CATEGORY_COND_BR || cat == ZYDIS_CATEGORY_UNCOND_BR);
		line.is_return = (cat == ZYDIS_CATEGORY_RET);

		bool is_terminator = (cat == ZYDIS_CATEGORY_UNCOND_BR || cat == ZYDIS_CATEGORY_RET ||
		                      mnem == ZYDIS_MNEMONIC_UD2 || mnem == ZYDIS_MNEMONIC_HLT);
		if (!is_terminator) {
			line.next_address = runtime_addr + line.length;
		}

		for (uint8_t op_idx = 0; op_idx < instruction.info.operand_count_visible; ++op_idx) {
			const auto &op = instruction.operands[op_idx];
			bool is_rel_imm = (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative);
			bool is_rip_mem = (op.type == ZYDIS_OPERAND_TYPE_MEMORY &&
			                   (op.mem.base == ZYDIS_REGISTER_RIP || op.mem.base == ZYDIS_REGISTER_EIP));
			if (!is_rel_imm && !is_rip_mem) {
				continue;
			}
			int64_t rel_disp = is_rel_imm ? op.imm.value.s : op.mem.disp.value;
			uint64_t target_vaddr = static_cast<uint64_t>(static_cast<int64_t>(runtime_addr + line.length) + rel_disp);
			if (!is_64_bit_addr) {
				target_vaddr &= 0xFFFFFFFFULL;
			}
			if (is_rel_imm) {
				line.alt_next_address = target_vaddr;
			} else {
				line.is_rip_relative_mem = true;
				line.memory_target_address = target_vaddr;
			}
			break;
		}
		return line;
	}
#else
	(void)mode;
	(void)syntax;
	(void)is_64_bit_addr;
#endif

	line.type = disasm_line_type::data_byte;
	line.length = 1;
	line.bytes[0] = buffer[offset];
	line.next_address = runtime_addr + 1;
	line.text = std::format("db 0x{:02x}", buffer[offset]);
	return line;
}

disasm_line disasm_line::make_function_header(uint64_t address,
                                              size_t file_offset,
                                              std::string func_name,
                                              uint32_t sub_order)
{
	disasm_line line;
	line.address = address;
	line.file_offset = file_offset;
	line.type = disasm_line_type::function_header;
	line.sub_order = sub_order;
	line.indent_depth = 0;
	line.label_name = std::move(func_name);
	line.text = line.label_name;
	return line;
}

disasm_line disasm_line::make_label(uint64_t address, size_t file_offset, std::string label_name, uint32_t sub_order)
{
	disasm_line line;
	line.address = address;
	line.file_offset = file_offset;
	line.type = disasm_line_type::block_label;
	line.sub_order = sub_order;
	line.indent_depth = 0;
	line.label_name = std::move(label_name);
	line.text = line.label_name;
	return line;
}

disasm_line disasm_line::make_source_location(uint64_t address, size_t file_offset, std::string source_loc, uint32_t sub_order)
{
	disasm_line line;
	line.address = address;
	line.file_offset = file_offset;
	line.type = disasm_line_type::source_location;
	line.sub_order = sub_order;
	line.indent_depth = 0;
	line.source_location = std::move(source_loc);
	line.text = line.source_location;
	return line;
}

disasm_line disasm_line::make_comment(uint64_t address, size_t file_offset, std::string comment_text, uint32_t sub_order)
{
	disasm_line line;
	line.address = address;
	line.file_offset = file_offset;
	line.type = disasm_line_type::comment;
	line.sub_order = sub_order;
	line.indent_depth = 0;
	line.text = std::move(comment_text);
	return line;
}

void disasm_line::populate_control_flow_labels(std::vector<disasm_line> &lines)
{
	if (lines.empty()) {
		return;
	}

	std::ranges::stable_sort(lines);

	// Map from target instruction virtual address to index in `lines`
	std::map<uint64_t, size_t> addr_to_index;
	std::vector<disasm_line> synthetic_lines;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (lines[i].is_instruction_or_data()) {
			addr_to_index[lines[i].address] = i;
			if (lines[i].has_symbol_context && lines[i].symbol_offset == 0 && !lines[i].symbol_name.empty()) {
				disasm_line fn_line =
				    make_function_header(lines[i].address, lines[i].file_offset, lines[i].symbol_name);
				fn_line.has_symbol_context = true;
				fn_line.symbol_name = lines[i].symbol_name;
				fn_line.symbol_offset = 0;
				synthetic_lines.push_back(std::move(fn_line));
			}
		}
	}

	// Collect all intra-range branch targets and record incoming branch sources
	std::map<uint64_t, std::vector<uint64_t>> target_to_sources;
	for (const auto &line : lines) {
		if (line.is_branch && line.alt_next_address.has_value()) {
			uint64_t target = *line.alt_next_address;
			if (addr_to_index.contains(target)) {
				target_to_sources[target].push_back(line.address);
			}
		}
	}

	if (!target_to_sources.empty()) {
		// Assign sequential L1, L2, ... labels in ascending address order
		std::map<uint64_t, std::string> target_labels;
		size_t label_counter = 1;
		for (const auto &[target_addr, sources] : target_to_sources) {
			std::string lbl = std::format("L{}", label_counter++);
			target_labels[target_addr] = lbl;
			size_t idx = addr_to_index[target_addr];
			lines[idx].label_name = lbl;
			lines[idx].branch_sources = sources;
		}

		// Decorate branching instructions with the resolved local label
		for (auto &line : lines) {
			if (line.is_branch && line.alt_next_address.has_value()) {
				auto it = target_labels.find(*line.alt_next_address);
				if (it != target_labels.end()) {
					if (line.target_annotation.empty()) {
						line.target_annotation = std::format(" <{}>", it->second);
					} else {
						line.target_annotation += std::format(" ({})", it->second);
					}
				}
			}
		}

		// Insert synthetic block_label lines that share the target instruction's address and sort before it
		for (const auto &[target_addr, sources] : target_to_sources) {
			const auto &target_inst = lines[addr_to_index[target_addr]];
			disasm_line lbl_line = make_label(target_addr, target_inst.file_offset, target_labels[target_addr]);
			lbl_line.has_symbol_context = target_inst.has_symbol_context;
			lbl_line.symbol_name = target_inst.symbol_name;
			lbl_line.symbol_offset = target_inst.symbol_offset;
			lbl_line.branch_sources = sources;
			synthetic_lines.push_back(std::move(lbl_line));
		}
	}

	if (!synthetic_lines.empty()) {
		lines.insert(lines.end(),
		             std::make_move_iterator(synthetic_lines.begin()),
		             std::make_move_iterator(synthetic_lines.end()));
		std::ranges::stable_sort(lines);
	}
}

std::string disasm_line::format_address_column() const
{
	if (!is_instruction_or_data()) {
		return "";
	}
	if (has_symbol_context) {
		return std::format("0x{:x} <+0x{:x}>:", address, symbol_offset);
	}
	return std::format("0x{:x}:", address);
}

std::string disasm_line::format_bytes_column() const
{
	if (!is_instruction_or_data() || length == 0) {
		return "";
	}
	size_t count = std::min<size_t>(length, bytes.size());
	std::string out;
	for (size_t i = 0; i < count; ++i) {
		if (i > 0) {
			out += " ";
		}
		out += std::format("{:02x}", bytes[i]);
	}
	return out;
}

std::string disasm_line::format_instruction_column() const
{
	switch (type) {
	case disasm_line_type::function_header:
		return std::format("{}:", label_name.empty() ? text : label_name);
	case disasm_line_type::block_label: {
		std::string from_str;
		if (!branch_sources.empty()) {
			from_str = " # from ";
			for (size_t i = 0; i < branch_sources.size(); ++i) {
				if (i > 0) {
					from_str += ", ";
				}
				from_str += std::format("0x{:x}", branch_sources[i]);
			}
		}
		return std::format("{}:{}", label_name, from_str);
	}
	case disasm_line_type::source_location:
		return std::format("# {}", source_location);
	case disasm_line_type::comment:
		return std::format("# {}", text);
	case disasm_line_type::instruction:
	case disasm_line_type::data_byte:
		return text + target_annotation;
	}
	return text;
}

std::string disasm_line::format_markdown_row() const
{
	constexpr size_t k_spaces_per_indent_level = 2;
	const std::string code_indent(indent_depth * k_spaces_per_indent_level, ' ');

	const std::string addr_col = format_address_column();
	const std::string bytes_col = format_bytes_column();
	const std::string inst_col = format_instruction_column();
	if (has_symbol_context) {
		return std::format("{:<18} {:<21} {}{}\n", addr_col, bytes_col, code_indent, inst_col);
	}
	return std::format("{:<10} {:<21} {}{}\n", addr_col, bytes_col, code_indent, inst_col);
}
} // namespace tools
