#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tools
{

/**
 * Discriminator for disassembled listing lines.
 * Ordered by display priority when multiple lines share the same virtual address,
 * so sorting a vector of disasm_line places source locations, headers, labels, and
 * comments immediately before the instruction at that address.
 */
enum class disasm_line_type : uint8_t {
	source_location = 0,
	function_header = 1,
	block_label = 2,
	comment = 3,
	instruction = 4,
	data_byte = 5,
};

/**
 * Represents a single logical line in a disassembled listing (an instruction,
 * raw data byte, basic-block label, DWARF source line, or comment) along with
 * its control-flow edges and resolved symbolic annotations.
 */
class disasm_line
{
      public:
	uint64_t address{0};
	size_t file_offset{0};
	size_t length{0};
	disasm_line_type type{disasm_line_type::instruction};
	uint32_t sub_order{0};

	// Logical indentation depth in abstract levels (0 = label/header at left margin,
	// 1 = top-level instruction, 2+ = nested loop or conditional body).
	// Visual rendering of levels into spaces is handled solely at formatting time.
	uint32_t indent_depth{1};
	std::array<uint8_t, 15> bytes{};

	// Control-flow graph edges:
	// - next_address: fallthrough instruction address (nullopt for unconditional jmp, ret, ud2, hlt, or noreturn calls)
	// - alt_next_address: branch or call target virtual address
	std::optional<uint64_t> next_address;
	std::optional<uint64_t> alt_next_address;

	// RIP/EIP-relative memory operand target address (e.g. .rodata string literal or global variable)
	std::optional<uint64_t> memory_target_address;

	bool is_call{false};
	bool is_branch{false};
	bool is_conditional{false};
	bool is_return{false};
	bool is_rip_relative_mem{false};

	// Enclosing function symbol context
	std::string symbol_name;
	uint64_t symbol_offset{0};
	bool has_symbol_context{false};

	// Primary text (decoded instruction mnemonic+operands, label name, source line, or comment)
	std::string text;

	// Assigned basic-block label (e.g. "L1") when targeted by intra-function branches
	std::string label_name;

	// Symbolic or string-literal annotation appended to the instruction (e.g. " <callee_func>", " <str#1>")
	std::string target_annotation;

	// DWARF source file:line location (if resolved), and optional trailing suffix comment
	// (e.g. " # line 38" when staying in the same file, or " # path/to/other.h:12" when switching files).
	std::string source_location;
	std::string source_comment_suffix;

	// Virtual addresses of instructions that branch to this address
	std::vector<uint64_t> branch_sources;

	// Decode a single instruction or fallback data byte from buffer[offset..]
	static disasm_line decode_one(std::span<const uint8_t> buffer,
	                              size_t offset,
	                              uint64_t runtime_addr,
	                              std::string_view mode,
	                              std::string_view syntax,
	                              bool is_64_bit_addr = true);

	// Construct synthetic non-instruction lines at a given address that sort before instructions
	static disasm_line make_function_header(uint64_t address,
	                                        size_t file_offset,
	                                        std::string func_name,
	                                        uint32_t sub_order = 0);
	static disasm_line make_label(uint64_t address,
	                              size_t file_offset,
	                              std::string label_name,
	                              uint32_t sub_order = 0);
	static disasm_line make_source_location(uint64_t address,
	                                        size_t file_offset,
	                                        std::string source_loc,
	                                        uint32_t sub_order = 0);
	static disasm_line make_comment(uint64_t address,
	                                size_t file_offset,
	                                std::string comment_text,
	                                uint32_t sub_order = 0);

	// Analyze intra-range branches across decoded lines, populate branch_sources, assign L<N> labels,
	// insert function_header and block_label lines before target instructions, and sort deterministically.
	static void populate_control_flow_labels(std::vector<disasm_line> &lines);

	// Resolve DWARF file:line source anchors for instructions in `lines` in batches via address_lookup,
	// bailing out early if the probe batch returns mostly "??", emit a header comment at function entry
	// ("# Function <name> at file <file> lines <from> - <to>"), and attach trailing "# line <N>" or
	// "# <file>:<N>" suffix comments to instructions when the source line transitions.
	static void populate_source_locations(std::vector<disasm_line> &lines, std::string_view elf_path);

	constexpr bool is_instruction_or_data() const noexcept
	{
		return type == disasm_line_type::instruction || type == disasm_line_type::data_byte;
	}

	constexpr bool falls_through() const noexcept
	{
		return next_address.has_value();
	}

	std::string format_address_column() const;
	std::string format_bytes_column() const;
	std::string format_instruction_column(bool brief = false) const;
	std::string format_markdown_row(bool brief = false) const;

	constexpr std::strong_ordering operator<=>(const disasm_line &other) const noexcept
	{
		if (auto cmp = address <=> other.address; cmp != 0) {
			return cmp;
		}
		if (auto cmp = static_cast<uint8_t>(type) <=> static_cast<uint8_t>(other.type); cmp != 0) {
			return cmp;
		}
		return sub_order <=> other.sub_order;
	}

	constexpr bool operator==(const disasm_line &other) const noexcept
	{
		return address == other.address && type == other.type && sub_order == other.sub_order;
	}
};

} // namespace tools
