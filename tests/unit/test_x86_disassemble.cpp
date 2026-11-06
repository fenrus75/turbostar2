// Tested source file: src/plugins/asm86/x86_disassemble/disasm_line.cpp, src/plugins/asm86/x86_disassemble/x86_disassemble_entry.cpp, src/plugins/asm86/x86_disassemble/x86_disassemble_security.cpp
#include "test_watchdog.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "config_manager.h"
#include "plugins/asm86/x86_disassemble/disasm_line.h"
#include "project_manager.h"
using namespace agentlib;

extern "C" void register_x86_disassemble(void);

namespace
{
void write_u16_le(std::vector<uint8_t> &buf, size_t off, uint16_t val)
{
	buf[off] = val & 0xFF;
	buf[off + 1] = (val >> 8) & 0xFF;
}

void write_u32_le(std::vector<uint8_t> &buf, size_t off, uint32_t val)
{
	buf[off] = val & 0xFF;
	buf[off + 1] = (val >> 8) & 0xFF;
	buf[off + 2] = (val >> 16) & 0xFF;
	buf[off + 3] = (val >> 24) & 0xFF;
}

void write_u64_le(std::vector<uint8_t> &buf, size_t off, uint64_t val)
{
	write_u32_le(buf, off, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
	write_u32_le(buf, off + 4, static_cast<uint32_t>((val >> 32) & 0xFFFFFFFFULL));
}
} // namespace

int main(int argc, char **argv)
{
	(void)argc;
	test_watchdog::setup_watchdog(30);
	register_x86_disassemble();
	tool_registry::get_instance().register_tool_family("x86", "Activate when working with x86 assembly");
	project_manager::get_instance().initialize();


	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.properties.active_families = {"base", "x86"};

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::read);

	std::cout << "Testing x86_disassemble tool..." << std::endl;

	// 1. Check family registration (get_family should return "x86")
	auto families = registry.get_all_registered_families();
	assert(std::ranges::find(families, "x86") != families.end() && "x86 family should be registered!");

	// 2. Test family security check: should fail if family is inactive
	bool x86_active = false;
	ctx.is_family_active = [&](const std::string &fam) {
		if (fam == "x86") {
			return x86_active;
		}
		return true;
	};

	std::string valid_hex_args = R"({"data": "89 c0"})";
	{
		auto prep = registry.prepare_tool("x86_disassemble", valid_hex_args, ctx);
		assert(prep.tool == nullptr && "Should fail prepare when x86 family is inactive");
		assert(prep.error_message.find("not active") != std::string::npos);
	}

	// Activate family and prepare should succeed
	x86_active = true;
	{
		auto prep = registry.prepare_tool("x86_disassemble", valid_hex_args, ctx);
		assert(prep.tool != nullptr && "Should succeed prepare when x86 family is active");
		assert(prep.error_message.empty());
	}

	// 3. Test Intel 64-bit Hex Disassembly
	{
		std::string args = R"({"data": "89 c0", "format": "hex", "mode": "64", "syntax": "intel"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Intel 64-bit Hex Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("```asm\n") != std::string::npos);
		assert(res.find("89 c0") != std::string::npos);
		assert(res.find("mov") != std::string::npos);
		assert(res.find("eax") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 4. Test AT&T 64-bit Hex Disassembly (also verify AT&T is the default syntax)
	{
		std::string args = R"({"data": "89 c0", "format": "hex", "mode": "64"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "AT&T 64-bit Hex Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("```asm\n") != std::string::npos);
		assert(res.find("89 c0") != std::string::npos);
		assert(res.find("mov") != std::string::npos);
		assert(res.find("%eax") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 5. Test Base64 input parsing (using auto format detection)
	{
		// "icA=" is base64 for "\x89\xc0"
		std::string args = R"({"data": "icA=", "format": "auto", "mode": "64", "syntax": "intel"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Base64 Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("89 c0") != std::string::npos);
		assert(res.find("mov") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 6. Test contiguous hex input parsing (using auto format detection)
	{
		std::string args = R"({"data": "89c090", "format": "auto", "mode": "64", "syntax": "intel"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Contiguous Hex Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("89 c0") != std::string::npos);
		assert(res.find("90") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 7. Test ELF binary disassembly by path + symbol (with auto symbol size, call target, local branch, and string table)
	{
		std::string test_file = (std::filesystem::current_path() / "test_x86_disasm_elf_temp.bin").string();
		std::vector<uint8_t> data(800, 0);
		data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
		data[4] = 2; // 64-bit
		data[5] = 1; // LSB
		data[6] = 1;
		write_u16_le(data, 16, 2);  // ET_EXEC
		write_u16_le(data, 18, 62); // EM_X86_64
		write_u32_le(data, 20, 1);
		write_u64_le(data, 40, 128); // e_shoff = 128
		write_u16_le(data, 52, 64);  // e_ehsize = 64
		write_u16_le(data, 58, 64);  // e_shentsize = 64
		write_u16_le(data, 60, 5);   // e_shnum = 5 (null, .text, .symtab, .shstrtab, .rodata)
		write_u16_le(data, 62, 3);   // e_shstrndx = 3

		const std::string strtab("\0.text\0.symtab\0.shstrtab\0my_func\0callee_func\0.rodata\0", 53);
		for (size_t i = 0; i < strtab.size(); ++i) {
			data[500 + i] = static_cast<uint8_t>(strtab[i]);
		}

		// Section 1: .text at offset 448 (0x1C0), vaddr 0x401000, size 32
		write_u32_le(data, 128 + 64, 1);
		write_u32_le(data, 128 + 64 + 4, 1);
		write_u64_le(data, 128 + 64 + 8, 6);
		write_u64_le(data, 128 + 64 + 16, 0x401000);
		write_u64_le(data, 128 + 64 + 24, 448);
		write_u64_le(data, 128 + 64 + 32, 32);

		// Section 2: .symtab at offset 600, size 72, sh_link = 3
		write_u32_le(data, 128 + 128, 7);
		write_u32_le(data, 128 + 128 + 4, 2);
		write_u64_le(data, 128 + 128 + 24, 600);
		write_u64_le(data, 128 + 128 + 32, 72);
		write_u32_le(data, 128 + 128 + 40, 3);
		write_u64_le(data, 128 + 128 + 56, 24);

		// Section 3: .shstrtab at offset 500, size 53
		write_u32_le(data, 128 + 192, 15);
		write_u32_le(data, 128 + 192 + 4, 3);
		write_u64_le(data, 128 + 192 + 24, 500);
		write_u64_le(data, 128 + 192 + 32, 53);

		// Section 4: .rodata at offset 700, vaddr 0x402000, size 32
		write_u32_le(data, 128 + 256, 45);
		write_u32_le(data, 128 + 256 + 4, 1);
		write_u64_le(data, 128 + 256 + 8, 2);
		write_u64_le(data, 128 + 256 + 16, 0x402000);
		write_u64_le(data, 128 + 256 + 24, 700);
		write_u64_le(data, 128 + 256 + 32, 32);

		const std::string ro_str = "hello_disasm_rodata";
		for (size_t i = 0; i < ro_str.size(); ++i) {
			data[700 + i] = static_cast<uint8_t>(ro_str[i]);
		}
		data[700 + ro_str.size()] = 0;

		// Symbol 1: my_func at 0x401000, size 16
		write_u32_le(data, 624, 25);
		data[624 + 4] = 0x12;
		write_u16_le(data, 624 + 6, 1);
		write_u64_le(data, 624 + 8, 0x401000);
		write_u64_le(data, 624 + 16, 16);

		// Symbol 2: callee_func at 0x401010, size 16
		write_u32_le(data, 648, 33);
		data[648 + 4] = 0x12;
		write_u16_le(data, 648 + 6, 1);
		write_u64_le(data, 648 + 8, 0x401010);
		write_u64_le(data, 648 + 16, 16);

		// Instructions in my_func (448..463, vaddr 0x401000..0x40100F):
		// 448..449: 74 0C (jz 0x40100E <+0xE>)
		// 450..454: E8 09 00 00 00 (call 0x401010 <callee_func>)
		// 455..461: 48 8D 3D F2 0F 00 00 (lea 0xFF2(%rip), %rdi -> 0x402000 <str#1>)
		// 462: 90 (nop)
		// 463: C3 (ret)
		data[448] = 0x74; data[449] = 0x0C;
		data[450] = 0xE8; data[451] = 0x09; data[452] = 0x00; data[453] = 0x00; data[454] = 0x00;
		data[455] = 0x48; data[456] = 0x8D; data[457] = 0x3D;
		data[458] = 0xF2; data[459] = 0x0F; data[460] = 0x00; data[461] = 0x00;
		data[462] = 0x90;
		data[463] = 0xC3;

		{
			std::ofstream out(test_file, std::ios::binary);
			out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
		}

		std::string args = std::format(R"({{"path": "{}", "symbol": "my_func"}})", test_file);
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "ELF Symbol Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("### Disassembly: `my_func`") != std::string::npos);
		assert(res.find("```asm\n") != std::string::npos);
		assert(res.find("                                         my_func:\n") != std::string::npos);
		assert(res.find("0x401000 <+0x0>:   74 0c                   jz L1 (0x40100E <+0xE>)\n") != std::string::npos);
		assert(res.find("                                         L1: # from 0x401000\n") != std::string::npos);
		assert(res.find("0x40100e <+0xe>:   90                      nop\n") != std::string::npos);
		assert(res.find("<callee_func>") != std::string::npos);
		assert(res.find("<str#1>") != std::string::npos);
		assert(res.find("#### Referenced String Literals") != std::string::npos);
		assert(res.find("| `str#1` | `0x402000` | `\"hello_disasm_rodata\"` |") != std::string::npos);
		// Should stop at my_func's size (16 bytes -> 0x401000..0x40100f), not spilling into 0x401010
		assert(res.find("0x401010 <+0x0>:") == std::string::npos);
#endif
		std::filesystem::remove(test_file);
	}

	// 8. Test invalid inputs (should report error during preparation or execution)
	{
		// Invalid hex characters in hex format
		std::string args = R"({"data": "89 zz", "format": "hex"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		assert(res.find("Error") != std::string::npos);

		// Invalid base64 character
		std::string b64_args = R"({"data": "icA?", "format": "base64"})";
		std::string b64_res = registry.execute_tool("x86_disassemble", b64_args, ctx);
		assert(b64_res.find("Error") != std::string::npos);
	}

	// 9. Test system prompt modification with x86 tool family table
	{
		auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
		auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);

		agent->inject_context("system", "Test original system prompt", false);
		auto conv = agent->get_conversation();
		assert(!conv.empty());
		std::string sys_prompt = conv[0].content;
		std::cout << "Generated System Prompt:\n" << sys_prompt << "\n";

		// Verify description and table presence
		assert(sys_prompt.find("Test original system prompt") != std::string::npos);
		assert(sys_prompt.find("*** ACTIVE TOOL FAMILIES ***") != std::string::npos);
		assert(sys_prompt.find("activate_tool_family") != std::string::npos);
		assert(sys_prompt.find("| Tool Family | When to Activate |") != std::string::npos);
		assert(sys_prompt.find("| x86 | Activate when working with x86 assembly |") != std::string::npos);
	}

	// 10. Test disasm_line IR control-flow edges and deterministic same-address sorting
	{
#ifdef HAVE_ZYDIS
		// 74 0C (jz +0xC), EB 04 (jmp +0x4), C3 (ret)
		const std::vector<uint8_t> code_bytes = {0x74, 0x0C, 0xEB, 0x04, 0xC3};
		auto cond_line = tools::disasm_line::decode_one(code_bytes, 0, 0x1000, "64", "att", true);
		assert(cond_line.address == 0x1000);
		assert(cond_line.length == 2);
		assert(cond_line.is_branch && cond_line.is_conditional);
		assert(cond_line.next_address.has_value() && *cond_line.next_address == 0x1002);
		assert(cond_line.alt_next_address.has_value() && *cond_line.alt_next_address == 0x100E);

		auto jmp_line = tools::disasm_line::decode_one(code_bytes, 2, 0x1002, "64", "att", true);
		assert(jmp_line.address == 0x1002);
		assert(jmp_line.length == 2);
		assert(jmp_line.is_branch && !jmp_line.is_conditional);
		assert(!jmp_line.next_address.has_value());
		assert(jmp_line.alt_next_address.has_value() && *jmp_line.alt_next_address == 0x1008);

		auto ret_line = tools::disasm_line::decode_one(code_bytes, 4, 0x1004, "64", "att", true);
		assert(ret_line.is_return);
		assert(!ret_line.next_address.has_value());
		assert(!ret_line.alt_next_address.has_value());

		std::vector<tools::disasm_line> seq = {
		    ret_line,
		    tools::disasm_line::make_label(0x1004, 4, ".L_ret"),
		    tools::disasm_line::make_source_location(0x1004, 4, "src/foo.cpp:10"),
		};
		std::ranges::stable_sort(seq);
		assert(seq[0].type == tools::disasm_line_type::source_location);
		assert(seq[1].type == tools::disasm_line_type::block_label);
		assert(seq[2].type == tools::disasm_line_type::instruction);
		assert(seq[1].indent_depth == 0);
		assert(seq[2].indent_depth == 1);

		// Verify indent_depth > 1 keeps Address/Bytes aligned and indents the Instruction column
		// Verify indent_depth aligns labels (depth 0) at the code column and instructions (depth 1, 2) indented by 2*depth spaces
		auto nested_line = ret_line;
		nested_line.indent_depth = 2;
		assert(seq[1].format_markdown_row() == "                                 .L_ret:\n");
		assert(ret_line.format_markdown_row() == "0x1004:    c3                      ret\n");
		assert(nested_line.format_markdown_row() == "0x1004:    c3                        ret\n");
#endif
	}

	// 11. Test recursive flow + bounded gap-fill with inline data byte before jump target
	{
		// 0x0: EB 01 (jmp 0x3)
		// 0x2: E8    (1-byte inline data gap; linear disasm would misdecode E8 C3 90 90 90 as a 5-byte call eating 'ret'!)
		// 0x3: C3    (ret - reached by jmp 0x3 in Phase 1)
		// 0x4: 90 90 90 (nops in Phase 2 gap after ret)
		std::string args = R"({"data": "eb 01 e8 c3 90 90 90", "format": "hex", "mode": "64", "syntax": "att"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Recursive + Gap-Fill Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("0x0:       eb 01                   jmp L1 (0x3)\n") != std::string::npos);
		assert(res.find("0x2:       e8                      db 0xe8\n") != std::string::npos);
		assert(res.find("                                 L1: # from 0x0\n") != std::string::npos);
		assert(res.find("0x3:       c3                      ret\n") != std::string::npos);
#endif
	}

	// 12. Test sequential L1, L2 branch labels with forward jump and backward loop edge
	{
		// 0x0: 74 04 (jz 0x6 -> L2)
		// 0x2: 90    (nop -> L1 target of loop at 0x3)
		// 0x3: 75 FD (jnz 0x2 -> L1 backward loop)
		// 0x5: C3    (ret)
		// 0x6: C3    (ret -> L2 target of forward branch at 0x0)
		std::string args = R"({"data": "74 04 90 75 fd c3 c3", "format": "hex", "mode": "64", "syntax": "att"})";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Sequential Labels (L1, L2) Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("0x0:       74 04                   jz L2 (0x6)\n") != std::string::npos);
		assert(res.find("                                 L1: # from 0x3\n") != std::string::npos);
		assert(res.find("0x2:       90                      nop\n") != std::string::npos);
		assert(res.find("0x3:       75 fd                   jnz L1 (0x2)\n") != std::string::npos);
		assert(res.find("                                 L2: # from 0x0\n") != std::string::npos);
		assert(res.find("0x6:       c3                      ret\n") != std::string::npos);
		// Verify L1 appears before its target instruction at 0x2, and L2 appears before its target instruction at 0x6
		assert(res.find("L1:") < res.find("0x2:       90                      nop\n"));
		assert(res.find("L2:") < res.find("0x6:       c3                      ret\n"));
#endif
	}

	// 13. Test DWARF source file:line anchor comments on real ELF binary (argv[0])
	if (argv && argv[0] && std::filesystem::exists(argv[0])) {
		std::string self_bin = std::filesystem::canonical(argv[0]).string();
		ctx.fs_security.add_allowed_root(std::filesystem::path(self_bin).parent_path(), access_type::read);
		std::string args = std::format(R"({{"path": "{}", "symbol": "write_u64_le"}})", self_bin);
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Self Binary DWARF Source Anchors Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		if (std::filesystem::exists("/usr/bin/addr2line") || std::filesystem::exists("/usr/bin/eu-addr2line")) {
			assert(res.find("# Function ") != std::string::npos);
			assert(res.find(" at file ") != std::string::npos);
			assert(res.find("test_x86_disassemble.cpp lines 36 - 40") != std::string::npos);
			assert(res.find(" # line 37\n") != std::string::npos);
			assert(res.find(" # line 38\n") != std::string::npos);
			assert(res.find(" # line 39\n") != std::string::npos);
			assert(res.find(" # line 40\n") != std::string::npos);
		}
#endif
	}

	std::cout << "x86_disassemble tool verified successfully!" << std::endl;
	return 0;
}
