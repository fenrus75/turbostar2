#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "fs_utils.h"
#include "hex/elf.h"
#include "plugins/asm86/x86_disassemble/disasm_line.h"
#include "utf8.h"
#include "x86_disassemble.h"
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

		if (file_size > static_cast<std::streamsize>(256ULL * 1024 * 1024)) {
			set_failure(ctx, "File is too large (>256MB).");
			return "Error: File is too large (>256MB) to read.";
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
		std::string effective_mode = args_.mode;
		if (is_elf && !args_.mode_specified) {
			effective_mode = elf.is_64_bit() ? "64" : "32";
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

		// Phase 1 (Recursive control-flow traversal) + Phase 2 (Gap-fill for switch/jump-table blocks & landing pads)
		std::vector<disasm_line> lines;
		std::vector<referenced_string_literal> string_literals;
		std::vector<bool> covered(limit - start_offset, false);
		std::deque<size_t> worklist;

		auto enqueue_offset = [&](size_t off) {
			if (off >= start_offset && off < limit && !covered[off - start_offset]) {
				worklist.push_back(off);
			}
		};

		auto vaddr_to_offset = [&](uint64_t target_vaddr, const elf_hex_highlighter::parsed_symbol *cur_sym) -> std::optional<size_t> {
			if (is_elf) {
				for (const auto &sec : elf.get_sections()) {
					if (sec.addr > 0 && target_vaddr >= sec.addr && target_vaddr < sec.addr + sec.size) {
						size_t off = static_cast<size_t>(sec.offset + (target_vaddr - sec.addr));
						if (off >= start_offset && off < limit) {
							return off;
						}
						return std::nullopt;
					}
				}
				if (cur_sym && cur_sym->vaddr > 0 && target_vaddr >= cur_sym->vaddr) {
					uint64_t rel = target_vaddr - cur_sym->vaddr;
					if (rel <= file_data.size() - cur_sym->offset) {
						size_t off = static_cast<size_t>(cur_sym->offset + rel);
						if (off >= start_offset && off < limit) {
							return off;
						}
					}
				}
			}
			uint64_t base_addr = (args_.address != 0) ? args_.address : start_offset;
			if (target_vaddr >= base_addr && (target_vaddr - base_addr) < (limit - start_offset)) {
				return start_offset + static_cast<size_t>(target_vaddr - base_addr);
			}
			return std::nullopt;
		};

		auto drain_worklist = [&]() {
			while (!worklist.empty()) {
				size_t file_pos = worklist.front();
				worklist.pop_front();
				if (file_pos < start_offset || file_pos >= limit || covered[file_pos - start_offset]) {
					continue;
				}

				// Clamp Zydis buffer slice to the next already-decoded byte so gaps never overrun known basic blocks
				size_t clamp_end = file_data.size();
				for (size_t k = file_pos + 1; k < limit; ++k) {
					if (covered[k - start_offset]) {
						clamp_end = k;
						break;
					}
				}

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

				std::span<const uint8_t> clamped_buf(file_data.data(), clamp_end);
				disasm_line line = disasm_line::decode_one(
				    clamped_buf, file_pos, runtime_addr, effective_mode, args_.syntax, !is_elf || elf.is_64_bit());
				if (cur_sym) {
					line.has_symbol_context = true;
					line.symbol_name = cur_sym->name;
					line.symbol_offset = file_pos - cur_sym->offset;
				}

				if (is_elf && line.type == disasm_line_type::instruction) {
					std::optional<uint64_t> target_vaddr = line.alt_next_address.has_value()
					    ? line.alt_next_address
					    : line.memory_target_address;
					if (target_vaddr.has_value()) {
						line.target_annotation = elf.resolve_target_annotation(
						    file_data, *target_vaddr, line.is_rip_relative_mem, cur_sym, true);
						if (!line.target_annotation.empty()) {
							line.target_annotation = format_string_literal_annotation(
							    line.target_annotation, *target_vaddr, string_literals);
							if (line.is_call && (line.target_annotation.find("_Unwind_Resume") != std::string::npos ||
							                     line.target_annotation.find("__cxa_throw") != std::string::npos ||
							                     line.target_annotation.find("__cxa_rethrow") != std::string::npos ||
							                     line.target_annotation.find("abort@plt") != std::string::npos ||
							                     line.target_annotation.find("__assert_fail") != std::string::npos)) {
								line.next_address = std::nullopt;
							}
						}
					}
				}

				size_t step = (line.length > 0) ? line.length : 1;
				for (size_t b = 0; b < step && (file_pos + b) < limit; ++b) {
					covered[file_pos + b - start_offset] = true;
				}

				if (line.next_address.has_value()) {
					if (auto next_off = vaddr_to_offset(*line.next_address, cur_sym)) {
						enqueue_offset(*next_off);
					}
				}
				if ((line.is_branch || line.is_call) && line.alt_next_address.has_value()) {
					if (auto alt_off = vaddr_to_offset(*line.alt_next_address, cur_sym)) {
						enqueue_offset(*alt_off);
					}
				}

				lines.push_back(std::move(line));
			}
		};

		// Phase 1: Seed entry offset and any known ELF function symbols in [start_offset, limit)
		enqueue_offset(start_offset);
		if (is_elf) {
			for (const auto &sym : elf.get_symbols()) {
				if (sym.offset >= start_offset && sym.offset < limit) {
					enqueue_offset(sym.offset);
				}
			}
		}
		drain_worklist();

		// Phase 2: Find uncovered gaps (e.g. switch/jump-table targets or exception landing pads) and disassemble them
		for (size_t off = start_offset; off < limit; ++off) {
			if (!covered[off - start_offset]) {
				enqueue_offset(off);
				drain_worklist();
			}
		}

		// Phase 3: Populate sequential branch target labels (L1, L2, ...), DWARF source lines, and render output
		disasm_line::populate_control_flow_labels(lines);
		if (is_elf && (elf.find_section_by_name(".debug_line") != nullptr ||
		               elf.find_section_by_name(".zdebug_line") != nullptr)) {
			disasm_line::populate_source_locations(lines, args_.safe_path);
		}
		std::string result;
		if (resolved_sym) {
			result += std::format("### Disassembly: `{}` in {} [0x{:X} - 0x{:X}]\n\n",
			                      resolved_sym->name, args_.requested_path, start_offset, limit);
		} else {
			result += std::format("### Disassembly: {} [0x{:X} - 0x{:X}]\n\n",
			                      args_.requested_path, start_offset, limit);
		}
		result += "```asm\n";
		for (const auto &line : lines) {
			result += line.format_markdown_row();
		}
		result += "```\n";

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

	std::vector<disasm_line> lines;
	std::vector<bool> covered(bytes.size(), false);
	std::deque<size_t> worklist;

	auto enqueue_offset = [&](size_t off) {
		if (off < bytes.size() && !covered[off]) {
			worklist.push_back(off);
		}
	};

	auto drain_worklist = [&]() {
		while (!worklist.empty()) {
			size_t offset = worklist.front();
			worklist.pop_front();
			if (offset >= bytes.size() || covered[offset]) {
				continue;
			}

			size_t clamp_end = bytes.size();
			for (size_t k = offset + 1; k < bytes.size(); ++k) {
				if (covered[k]) {
					clamp_end = k;
					break;
				}
			}

			std::span<const uint8_t> clamped_buf(bytes.data(), clamp_end);
			disasm_line line = disasm_line::decode_one(
			    clamped_buf, offset, args_.address + offset, args_.mode, args_.syntax, args_.mode == "64");
			size_t step = (line.length > 0) ? line.length : 1;
			for (size_t b = 0; b < step && (offset + b) < bytes.size(); ++b) {
				covered[offset + b] = true;
			}

			if (line.next_address.has_value() && *line.next_address >= args_.address &&
			    (*line.next_address - args_.address) < bytes.size()) {
				enqueue_offset(static_cast<size_t>(*line.next_address - args_.address));
			}
			if ((line.is_branch || line.is_call) && line.alt_next_address.has_value() &&
			    *line.alt_next_address >= args_.address &&
			    (*line.alt_next_address - args_.address) < bytes.size()) {
				enqueue_offset(static_cast<size_t>(*line.alt_next_address - args_.address));
			}

			lines.push_back(std::move(line));
		}
	};

	// Phase 1: Recursive traversal from entry
	enqueue_offset(0);
	drain_worklist();

	// Phase 2: Gap-fill any remaining uncovered bytes
	for (size_t off = 0; off < bytes.size(); ++off) {
		if (!covered[off]) {
			enqueue_offset(off);
			drain_worklist();
		}
	}

	disasm_line::populate_control_flow_labels(lines);

	std::string result = "```asm\n";
	for (const auto &line : lines) {
		result += line.format_markdown_row();
	}
	result += "```\n";
	set_success(ctx, "Disassembled " + std::to_string(bytes.size()) + " bytes");
	return result;
#endif
}

} // namespace tools
