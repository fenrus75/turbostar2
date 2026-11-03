#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "fs_utils.h"
#include "hex/elf.h"
#include "utf8.h"
#include "x86_disassemble.h"

#ifdef HAVE_ZYDIS
#include <Zydis/Zydis.h>
#endif

namespace tools
{

namespace
{
struct referenced_string_literal {
	size_t id{0};
	std::string address;
	std::string value;
};

std::string format_string_literal_annotation(std::string_view target_annotation,
                                             uint64_t target_vaddr,
                                             std::vector<referenced_string_literal> &string_literals)
{
	if (!target_annotation.starts_with(" <\"") || !target_annotation.ends_with("\">") || target_annotation.size() < 5) {
		return std::string(target_annotation);
	}
	std::string_view lit_val = target_annotation.substr(3, target_annotation.size() - 5);
	std::string addr = std::format("0x{:X}", target_vaddr);
	size_t assigned_id = 0;
	for (const auto &entry : string_literals) {
		if (entry.value == lit_val && entry.address == addr) {
			assigned_id = entry.id;
			break;
		}
	}
	if (assigned_id == 0) {
		assigned_id = string_literals.size() + 1;
		string_literals.push_back(referenced_string_literal{
		    .id = assigned_id,
		    .address = std::move(addr),
		    .value = std::string(lit_val),
		});
	}
	return std::format(" <str#{}>", assigned_id);
}

std::string escape_markdown_pipes(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		if (c == '|') {
			out += "\\|";
		} else {
			out.push_back(c);
		}
	}
	return out;
}
} // namespace

static bool parse_ascii_hex(const std::string &input, std::vector<uint8_t> &out_bytes, std::string &out_error)
{
	std::vector<uint8_t> bytes;
	std::string token;
	std::istringstream iss(input);
	std::vector<std::string> tokens;
	while (iss >> token) {
		if (!token.empty() && token.back() == ',') {
			token.pop_back();
		}
		if (!token.empty()) {
			tokens.push_back(token);
		}
	}

	if (tokens.empty()) {
		out_error = "Input hex string is empty.";
		return false;
	}

	// If there is only one token, it is longer than 2 characters, has even length,
	// and consists entirely of hex digits, treat it as contiguous hex.
	if (tokens.size() == 1 && tokens[0].size() > 2 && tokens[0].size() % 2 == 0) {
		std::string t = tokens[0];
		bool all_hex = true;
		for (char c : t) {
			if (!std::isxdigit(static_cast<unsigned char>(c))) {
				all_hex = false;
				break;
			}
		}
		if (all_hex) {
			for (size_t i = 0; i < t.size(); i += 2) {
				std::string byte_str = t.substr(i, 2);
				bytes.push_back(static_cast<uint8_t>(std::strtol(byte_str.c_str(), nullptr, 16)));
			}
			out_bytes = std::move(bytes);
			return true;
		}
	}

	for (const auto &tok : tokens) {
		if (tok.size() > 2) {
			out_error = "Invalid hex token: \"" + tok + "\" (too long for a single byte)";
			return false;
		}
		char *endptr = nullptr;
		long val = std::strtol(tok.c_str(), &endptr, 16);
		if (*endptr != '\0' || val < 0 || val > 255) {
			out_error = "Invalid hex token: \"" + tok + "\"";
			return false;
		}
		bytes.push_back(static_cast<uint8_t>(val));
	}
	out_bytes = std::move(bytes);
	return true;
}

static bool parse_base64(const std::string &input, std::vector<uint8_t> &out_bytes, std::string &out_error)
{
	std::string stripped;
	stripped.reserve(input.size());
	for (char c : input) {
		if (!std::isspace(static_cast<unsigned char>(c))) {
			stripped.push_back(c);
		}
	}

	if (!utf8::is_alnum_only(stripped, "+/=")) {
		out_error = "Invalid character in Base64 input.";
		return false;
	}

	auto decoded = fs_utils::base64_decode(stripped);
	if (decoded.empty() && !stripped.empty()) {
		out_error = "Failed to decode Base64 input.";
		return false;
	}

	out_bytes.assign(decoded.begin(), decoded.end());
	return true;
}

x86_disassemble_tool::x86_disassemble_tool(x86_disassemble_args args)
    : llm_tool_action("Disassembling x86 machine code"), args_(std::move(args))
{
}

bool x86_disassemble_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string x86_disassemble_tool::execute(agentlib::tool_context &ctx)
{
#ifndef HAVE_ZYDIS
	set_failure(ctx, "Zydis support is not enabled in this build.");
	return "Error: Zydis disassembly library is not available in this build of Turbostar.";
#else
	ZydisMachineMode machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
	if (args_.mode == "32") {
		machine_mode = ZYDIS_MACHINE_MODE_LEGACY_32;
	} else if (args_.mode == "16") {
		machine_mode = ZYDIS_MACHINE_MODE_LEGACY_16;
	}

	if (!args_.safe_path.empty()) {
		auto vfs = ctx.fs_security.get_vfs();
		if (vfs && vfs->is_local_path_available(args_.safe_path)) {
			args_.safe_path = vfs->get_local_path(args_.safe_path);
		}

		if (!std::filesystem::exists(args_.safe_path)) {
			set_failure(ctx, "File does not exist.");
			return "Error: File does not exist.";
		}

		std::ifstream file(args_.safe_path, std::ios::binary);
		if (!file.is_open()) {
			set_failure(ctx, "Failed to open file for reading.");
			return "Error: Failed to open file for reading.";
		}

		file.seekg(0, std::ios::end);
		std::streamsize file_size = file.tellg();
		file.seekg(0, std::ios::beg);

		if (file_size > 50 * 1024 * 1024) {
			set_failure(ctx, "File is too large (>50MB).");
			return "Error: File is too large (>50MB) to read.";
		}

		std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
		if (file_size > 0 && !file.read(reinterpret_cast<char *>(file_data.data()), file_size)) {
			file.close();
			set_failure(ctx, "Failed to read file contents.");
			return "Error: Failed to read file contents.";
		}
		file.close();

		elf_hex_highlighter elf;
		bool is_elf = elf.can_handle(file_data) && elf.parse(file_data);
		if (is_elf && !args_.mode_specified) {
			machine_mode = elf.is_64_bit() ? ZYDIS_MACHINE_MODE_LONG_64 : ZYDIS_MACHINE_MODE_LEGACY_32;
		}

		size_t start_offset = args_.offset;
		size_t disasm_size = args_.size;
		const elf_hex_highlighter::parsed_symbol *resolved_sym = nullptr;

		if (!args_.symbol.empty()) {
			if (!is_elf) {
				set_failure(ctx, "File is not an ELF binary; cannot resolve symbol.");
				return "Error: File is not an ELF binary; cannot resolve symbol: " + args_.symbol;
			}
			resolved_sym = elf.find_symbol_by_name(args_.symbol);
			if (resolved_sym) {
				start_offset = resolved_sym->offset + args_.offset;
				if (disasm_size == 0) {
					disasm_size = (resolved_sym->size > 0) ? std::min<size_t>(resolved_sym->size, 4096) : 256;
				}
			} else if (const auto *sec = elf.find_section_by_name(args_.symbol)) {
				start_offset = sec->offset + args_.offset;
				if (disasm_size == 0) {
					disasm_size = (sec->size > 0) ? std::min<size_t>(sec->size, 4096) : 256;
				}
			} else {
				set_failure(ctx, "Could not resolve symbol or section: " + args_.symbol);
				return "Error: Could not resolve symbol or section: " + args_.symbol;
			}
		} else {
			if (is_elf && !elf.find_section_at_offset(start_offset)) {
				for (const auto &sec : elf.get_sections()) {
					if (sec.addr > 0 && start_offset >= sec.addr && start_offset < sec.addr + sec.size) {
						start_offset = sec.offset + (start_offset - sec.addr);
						break;
					}
				}
			}
			resolved_sym = is_elf ? elf.find_symbol_at_offset(start_offset) : nullptr;
			if (disasm_size == 0) {
				if (resolved_sym && resolved_sym->offset == start_offset && resolved_sym->size > 0) {
					disasm_size = std::min<size_t>(resolved_sym->size, 4096);
				} else {
					disasm_size = 256;
				}
			}
		}

		if (start_offset >= file_data.size()) {
			set_failure(ctx, "offset is out of bounds.");
			return "Error: offset is out of bounds.";
		}

		size_t limit = (disasm_size > file_data.size() - start_offset) ? file_data.size() : start_offset + disasm_size;
		std::string result;
		if (resolved_sym) {
			result += std::format("### Disassembly: `{}` in {} [0x{:X} - 0x{:X}]\n\n",
			                      resolved_sym->name, args_.requested_path, start_offset, limit);
		} else {
			result += std::format("### Disassembly: {} [0x{:X} - 0x{:X}]\n\n",
			                      args_.requested_path, start_offset, limit);
		}
		result += "| Address | Bytes | Instruction |\n| --- | --- | --- |\n";

		std::vector<referenced_string_literal> string_literals;
		size_t file_pos = start_offset;
		while (file_pos < limit) {
			const auto *cur_sec = is_elf ? elf.find_section_at_offset(file_pos) : nullptr;
			const auto *cur_sym = is_elf ? elf.find_symbol_at_offset(file_pos) : nullptr;
			uint64_t runtime_addr = file_pos;
			if (cur_sec && cur_sec->addr > 0) {
				runtime_addr = cur_sec->addr + (file_pos - cur_sec->offset);
			} else if (cur_sym && cur_sym->vaddr > 0) {
				runtime_addr = cur_sym->vaddr + (file_pos - cur_sym->offset);
			} else if (args_.address != 0) {
				runtime_addr = args_.address + (file_pos - start_offset);
			}

			ZydisDisassembledInstruction instruction;
			ZyanStatus status;
			if (args_.syntax == "intel") {
				status = ZydisDisassembleIntel(machine_mode, runtime_addr, file_data.data() + file_pos,
				                               file_data.size() - file_pos, &instruction);
			} else {
				status = ZydisDisassembleATT(machine_mode, runtime_addr, file_data.data() + file_pos,
				                             file_data.size() - file_pos, &instruction);
			}

			size_t length = 0;
			std::string instr_text;
			std::string target_annotation;
			if (ZYAN_SUCCESS(status)) {
				length = instruction.info.length;
				instr_text = elf_hex_highlighter::shorten_hex(instruction.text);
				if (is_elf) {
					for (uint8_t op_idx = 0; op_idx < instruction.info.operand_count_visible; ++op_idx) {
						const auto &op = instruction.operands[op_idx];
						bool is_rel_imm = (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative);
						bool is_rip_mem = (op.type == ZYDIS_OPERAND_TYPE_MEMORY &&
						                   (op.mem.base == ZYDIS_REGISTER_RIP || op.mem.base == ZYDIS_REGISTER_EIP));
						if (!is_rel_imm && !is_rip_mem) {
							continue;
						}
						int64_t rel_disp = is_rel_imm ? op.imm.value.s : op.mem.disp.value;
						uint64_t target_vaddr = static_cast<uint64_t>(static_cast<int64_t>(runtime_addr + length) + rel_disp);
						if (!elf.is_64_bit()) {
							target_vaddr &= 0xFFFFFFFFULL;
						}
						target_annotation = elf.resolve_target_annotation(file_data, target_vaddr, is_rip_mem, cur_sym, true);
						if (!target_annotation.empty()) {
							target_annotation = format_string_literal_annotation(target_annotation, target_vaddr, string_literals);
							break;
						}
					}
				}
			} else {
				length = 1;
				instr_text = std::format("db 0x{:02x}", file_data[file_pos]);
			}
			if (length == 0) {
				length = 1;
			}

			size_t avail_len = std::min(length, file_data.size() - file_pos);
			std::string bytes_str;
			for (size_t i = 0; i < avail_len; ++i) {
				if (i > 0) {
					bytes_str += " ";
				}
				bytes_str += std::format("{:02x}", file_data[file_pos + i]);
			}

			std::string addr_col = cur_sym
			    ? std::format("`0x{:x}` `<+0x{:x}>`", runtime_addr, file_pos - cur_sym->offset)
			    : std::format("`0x{:x}`", runtime_addr);
			std::string full_instr = escape_markdown_pipes(instr_text + target_annotation);
			result += std::format("| {} | {} | {} |\n", addr_col, bytes_str, full_instr);
			file_pos += avail_len;
		}

		if (!string_literals.empty()) {
			result += "\n#### Referenced String Literals\n";
			result += "| ID | Address | Value |\n";
			result += "| --- | --- | --- |\n";
			for (const auto &entry : string_literals) {
				result += std::format("| `str#{}` | `{}` | `\"{}\"` |\n",
				                      entry.id,
				                      entry.address.empty() ? "-" : entry.address,
				                      escape_markdown_pipes(entry.value));
			}
		}

		set_success(ctx, std::format("Disassembled {} bytes", limit - start_offset));
		return result;
	}

	std::vector<uint8_t> bytes;
	std::string parse_error;
	bool parsed_ok = false;

	if (args_.format == "hex") {
		parsed_ok = parse_ascii_hex(args_.data, bytes, parse_error);
	} else if (args_.format == "base64") {
		parsed_ok = parse_base64(args_.data, bytes, parse_error);
	} else { // "auto"
		parsed_ok = parse_ascii_hex(args_.data, bytes, parse_error);
		if (!parsed_ok) {
			std::string b64_error;
			if (parse_base64(args_.data, bytes, b64_error)) {
				parsed_ok = true;
			} else {
				parse_error = "Could not parse input as hex (" + parse_error + ") or base64 (" + b64_error + ").";
			}
		}
	}

	if (!parsed_ok) {
		set_failure(ctx, parse_error);
		return "Error parsing input: " + parse_error;
	}

	std::string result = "| Address | Bytes | Instruction |\n| --- | --- | --- |\n";
	size_t offset = 0;

	while (offset < bytes.size()) {
		ZydisDisassembledInstruction instruction;
		ZyanStatus status;
		if (args_.syntax == "intel") {
			status = ZydisDisassembleIntel(machine_mode, args_.address + offset, bytes.data() + offset, bytes.size() - offset,
						       &instruction);
		} else {
			status = ZydisDisassembleATT(machine_mode, args_.address + offset, bytes.data() + offset, bytes.size() - offset,
						     &instruction);
		}

		size_t length = 0;
		std::string instr_text;
		if (ZYAN_SUCCESS(status)) {
			length = instruction.info.length;
			instr_text = elf_hex_highlighter::shorten_hex(instruction.text);
		} else {
			length = 1;
			instr_text = std::format("db 0x{:02x}", bytes[offset]);
		}
		if (length == 0) {
			length = 1;
		}

		std::string bytes_str;
		for (size_t i = 0; i < length; ++i) {
			if (i > 0) {
				bytes_str += " ";
			}
			bytes_str += std::format("{:02x}", bytes[offset + i]);
		}

		std::string escaped_instr = escape_markdown_pipes(instr_text);
		result += std::format("| `0x{:x}` | {} | {} |\n", args_.address + offset, bytes_str, escaped_instr);
		offset += length;
	}

	set_success(ctx, "Disassembled " + std::to_string(bytes.size()) + " bytes");
	return result;
#endif
}

} // namespace tools
