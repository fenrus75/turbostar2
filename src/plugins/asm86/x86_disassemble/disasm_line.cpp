#include "plugins/asm86/x86_disassemble/disasm_line.h"
#include <algorithm>
#include <format>
#include <map>
#include "address_lookup.h"
#include "codemap_fallbacks.h"
#include "codemap_utils.h"
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

		// Rewrite branching instructions to use the local label as primary operand: "jmp L1 (0x... <+0x...>)"
		for (auto &line : lines) {
			if (line.is_branch && line.alt_next_address.has_value()) {
				auto it = target_labels.find(*line.alt_next_address);
				if (it != target_labels.end()) {
					auto sp = line.text.find(' ');
					if (sp != std::string::npos) {
						std::string mnemonic = line.text.substr(0, sp);
						std::string orig_target = line.text.substr(sp + 1);
						line.text = std::format("{} {}", mnemonic, it->second);
						line.target_annotation = std::format(" ({}{})", orig_target, line.target_annotation);
					} else if (line.target_annotation.empty()) {
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

	// Rewrite resolved call instructions so the symbolic target is the primary operand:
	// "call <callee_func> (0x401010)" in full mode, and "call <callee_func>" in brief mode.
	for (auto &line : lines) {
		if (line.is_call && line.alt_next_address.has_value() && !line.target_annotation.empty()) {
			std::string_view ann = line.target_annotation;
			while (!ann.empty() && ann.front() == ' ') {
				ann.remove_prefix(1);
			}
			// Only rewrite if annotation is a resolved symbol name (not a self-relative <+0x...> offset)
			if (ann.starts_with('<') && !ann.starts_with("<+")) {
				auto sp = line.text.find(' ');
				if (sp != std::string::npos) {
					std::string mnemonic = line.text.substr(0, sp);
					std::string orig_target = line.text.substr(sp + 1);
					line.text = std::format("{} {}", mnemonic, ann);
					line.target_annotation = std::format(" ({})", orig_target);
				}
			}
		}
	}

	if (!synthetic_lines.empty()) {
		lines.insert(lines.end(),
		             std::make_move_iterator(synthetic_lines.begin()),
		             std::make_move_iterator(synthetic_lines.end()));
		std::ranges::stable_sort(lines);
	}
}

void disasm_line::populate_source_locations(std::vector<disasm_line> &lines, std::string_view elf_path)
{
	if (lines.empty() || elf_path.empty()) {
		return;
	}

	std::ranges::stable_sort(lines);

	std::vector<size_t> inst_indices;
	inst_indices.reserve(lines.size());
	for (size_t i = 0; i < lines.size(); ++i) {
		if (lines[i].is_instruction_or_data()) {
			inst_indices.push_back(i);
		}
	}
	if (inst_indices.empty()) {
		return;
	}

	// For small functions (<= 64 instructions), resolve in a single batch to avoid spawning addr2line twice.
	// For larger ranges (> 64 instructions), probe the first 16 instructions first and bail out early if >= 80% return "??".
	constexpr size_t k_single_batch_threshold = 64;
	constexpr size_t k_first_probe_size = 16;
	constexpr size_t k_subsequent_batch_size = 512;

	struct resolved_inst_loc {
		size_t line_idx;
		std::string file_path;
		int line_number;
	};
	std::vector<resolved_inst_loc> resolved_locs;
	resolved_locs.reserve(inst_indices.size());

	size_t cursor = 0;
	while (cursor < inst_indices.size()) {
		const size_t batch_limit = (cursor == 0 && inst_indices.size() > k_single_batch_threshold)
		    ? k_first_probe_size
		    : k_subsequent_batch_size;
		const size_t count = std::min(batch_limit, inst_indices.size() - cursor);

		std::vector<uintptr_t> batch_addrs;
		batch_addrs.reserve(count);
		for (size_t k = 0; k < count; ++k) {
			batch_addrs.push_back(static_cast<uintptr_t>(lines[inst_indices[cursor + k]].address));
		}

		auto resolved = turbostar::address_lookup::resolve_addresses(batch_addrs, elf_path);
		size_t unresolved_count = 0;

		for (size_t k = 0; k < count && k < resolved.size(); ++k) {
			const auto &res = resolved[k];
			if (!res.file_path.empty() && res.file_path != "??" && res.line_number > 0) {
				size_t idx = inst_indices[cursor + k];
				lines[idx].source_location = std::format("{}:{}", res.file_path, res.line_number);
				resolved_locs.push_back({idx, res.file_path, res.line_number});
			} else {
				++unresolved_count;
			}
		}

		cursor += count;

		// Early bailout if too many addresses came back as "??" (>= 80% on a probe batch)
		if (count >= 8 && unresolved_count * 5 >= count * 4) {
			break;
		}
	}

	if (resolved_locs.empty()) {
		return;
	}

	// Group resolved locations by function entry (or initial instruction) to emit
	// "# Function <name> at file <file> lines <from> - <to>" header comments and
	// attach trailing "# line <N>" or "# <file>:<N>" suffix comments to instructions.
	std::vector<disasm_line> header_comments;
	std::map<std::string, std::vector<codemap_symbol_info>> file_symbols_cache;

	std::string current_file;
	int current_line = -1;
	std::string current_func_symbol;
	bool emitted_initial_header = false;

	for (size_t r = 0; r < resolved_locs.size(); ++r) {
		const auto &rl = resolved_locs[r];
		auto &inst = lines[rl.line_idx];

		const bool is_new_function =
		    (inst.has_symbol_context && !inst.symbol_name.empty() &&
		     (inst.symbol_offset == 0 || inst.symbol_name != current_func_symbol)) ||
		    (!emitted_initial_header && r == 0);

		if (is_new_function) {
			emitted_initial_header = true;
			current_func_symbol = inst.symbol_name;
			const std::string &primary_file = rl.file_path;

			// Compute observed min/max line range within primary_file for this function
			int min_line = rl.line_number;
			int max_line = rl.line_number;
			for (size_t j = r + 1; j < resolved_locs.size(); ++j) {
				const auto &next_inst = lines[resolved_locs[j].line_idx];
				if (inst.has_symbol_context && next_inst.has_symbol_context &&
				    next_inst.symbol_name != inst.symbol_name) {
					break;
				}
				if (resolved_locs[j].file_path == primary_file) {
					min_line = std::min(min_line, resolved_locs[j].line_number);
					max_line = std::max(max_line, resolved_locs[j].line_number);
				}
			}

			// Query codemap fallback symbols for exact function start/end lines if file exists on disk
			auto sym_it = file_symbols_cache.find(primary_file);
			if (sym_it == file_symbols_cache.end()) {
				std::vector<codemap_symbol_info> syms;
				fallback_find_symbols(primary_file, 1, syms);
				sym_it = file_symbols_cache.emplace(primary_file, std::move(syms)).first;
			}
			if (const auto *enclosing = find_enclosing_symbol(sym_it->second, rl.line_number)) {
				min_line = enclosing->start_line;
				max_line = enclosing->end_line;
			} else if (!inst.symbol_name.empty()) {
				if (const auto *by_hint = find_symbol_by_hint(sym_it->second, inst.symbol_name)) {
					min_line = by_hint->start_line;
					max_line = by_hint->end_line;
				}
			}

			std::string header_text;
			if (!inst.symbol_name.empty()) {
				header_text = std::format(
				    "Function {} at file {} lines {} - {}", inst.symbol_name, primary_file, min_line, max_line);
			} else {
				header_text = std::format("File {} lines {} - {}", primary_file, min_line, max_line);
			}

			disasm_line hdr_line = make_source_location(inst.address, inst.file_offset, std::move(header_text));
			hdr_line.has_symbol_context = inst.has_symbol_context;
			hdr_line.symbol_name = inst.symbol_name;
			hdr_line.symbol_offset = inst.symbol_offset;
			header_comments.push_back(std::move(hdr_line));

			current_file = primary_file;
			current_line = -1; // Force first instruction to emit "# line <N>"
		}

		if (rl.file_path != current_file) {
			current_file = rl.file_path;
			current_line = rl.line_number;
			inst.source_comment_suffix = std::format(" # {}:{}", rl.file_path, rl.line_number);
		} else if (rl.line_number != current_line) {
			current_line = rl.line_number;
			inst.source_comment_suffix = std::format(" # line {}", rl.line_number);
		}
	}

	if (!header_comments.empty()) {
		lines.insert(lines.end(),
		             std::make_move_iterator(header_comments.begin()),
		             std::make_move_iterator(header_comments.end()));
		std::ranges::stable_sort(lines);
	}
}

bool disasm_line::is_noreturn_call_target(std::string_view target_annotation) noexcept
{
	while (!target_annotation.empty() && (target_annotation.front() == ' ' || target_annotation.front() == '<')) {
		target_annotation.remove_prefix(1);
	}
	while (!target_annotation.empty() && (target_annotation.back() == ' ' || target_annotation.back() == '>')) {
		target_annotation.remove_suffix(1);
	}
	if (target_annotation.ends_with("@plt")) {
		target_annotation.remove_suffix(4);
	}
	if (target_annotation.starts_with("::")) {
		target_annotation.remove_prefix(2);
	}
	if (target_annotation.starts_with("std::__throw_")) {
		return true;
	}
	constexpr std::array<std::string_view, 21> k_noreturn_symbols = {
	    "_Unwind_Resume",
	    "__cxa_throw",
	    "__cxa_rethrow",
	    "__cxa_bad_cast",
	    "__cxa_bad_typeid",
	    "__cxa_pure_virtual",
	    "__cxa_deleted_virtual",
	    "__cxa_call_unexpected",
	    "__cxa_throw_bad_array_new_length",
	    "std::terminate",
	    "std::unexpected",
	    "abort",
	    "__assert_fail",
	    "__assert_perror_fail",
	    "__stack_chk_fail",
	    "exit",
	    "_exit",
	    "_Exit",
	    "quick_exit",
	    "pthread_exit",
	    "longjmp",
	};
	for (std::string_view sym : k_noreturn_symbols) {
		if (target_annotation == sym) {
			return true;
		}
	}
	return false;
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

std::string disasm_line::format_instruction_column(bool brief) const
{
	switch (type) {
	case disasm_line_type::function_header:
		return std::format("{}:", label_name.empty() ? text : label_name);
	case disasm_line_type::block_label: {
		if (brief) {
			return std::format("{}:", label_name);
		}
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
	case disasm_line_type::data_byte: {
		std::string noreturn_suffix = (is_call && !next_address.has_value()) ? " # does not return" : "";
		if (brief && (is_branch || is_call) && target_annotation.starts_with(" (")) {
			return text + source_comment_suffix + noreturn_suffix;
		}
		return text + target_annotation + source_comment_suffix + noreturn_suffix;
	}
	}
	return text;
}

std::string disasm_line::format_markdown_row(bool brief) const
{
	constexpr size_t k_spaces_per_indent_level = 2;
	const std::string code_indent(indent_depth * k_spaces_per_indent_level, ' ');
	const std::string inst_col = format_instruction_column(brief);

	if (brief) {
		return std::format("{}{}\n", code_indent, inst_col);
	}

	const std::string addr_col = format_address_column();
	const std::string bytes_col = format_bytes_column();
	if (has_symbol_context) {
		return std::format("{:<18} {:<21} {}{}\n", addr_col, bytes_col, code_indent, inst_col);
	}
	return std::format("{:<10} {:<21} {}{}\n", addr_col, bytes_col, code_indent, inst_col);
}

} // namespace tools
