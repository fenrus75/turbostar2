#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);

	std::cout << "Testing x86_disassemble tool..." << std::endl;

	// 1. Check family registration (get_family should return "x86")
	auto families = registry.get_all_registered_families();
	assert(std::find(families.begin(), families.end(), "x86") != families.end() && "x86 family should be registered!");

	// 2. Test family security check: should fail if family is inactive
	bool x86_active = false;
	ctx.is_family_active = [&](const std::string &fam) {
		if (fam == "x86") {
			return x86_active;
		}
		return true;
	};

	std::string valid_hex_args = "{\"data\": \"89 c0\"}";
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
		std::string args = "{\"data\": \"89 c0\", \"format\": \"hex\", \"mode\": \"64\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Intel 64-bit Hex Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("| 89 c0 |") != std::string::npos);
		assert(res.find("mov") != std::string::npos);
		assert(res.find("eax") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 4. Test AT&T 64-bit Hex Disassembly
	{
		std::string args = "{\"data\": \"89 c0\", \"format\": \"hex\", \"mode\": \"64\", \"syntax\": \"att\"}";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "AT&T 64-bit Hex Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("| 89 c0 |") != std::string::npos);
		assert(res.find("mov") != std::string::npos);
		assert(res.find("%eax") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 5. Test Base64 input parsing (using auto format detection)
	{
		// "icA=" is base64 for "\x89\xc0"
		std::string args = "{\"data\": \"icA=\", \"format\": \"auto\", \"mode\": \"64\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Base64 Disassembly Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("| 89 c0 |") != std::string::npos);
		assert(res.find("mov") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 6. Test contiguous hex input parsing (using auto format detection)
	{
		std::string args = "{\"data\": \"89c090\", \"format\": \"auto\", \"mode\": \"64\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		std::cout << "Contiguous Hex Result:\n" << res << std::endl;
#ifdef HAVE_ZYDIS
		assert(res.find("| 89 c0 |") != std::string::npos);
		assert(res.find("| 90 |") != std::string::npos);
#else
		assert(res.find("not available") != std::string::npos);
#endif
	}

	// 7. Test invalid inputs (should report error during preparation or execution)
	{
		// Invalid hex characters in hex format
		std::string args = "{\"data\": \"89 zz\", \"format\": \"hex\"}";
		std::string res = registry.execute_tool("x86_disassemble", args, ctx);
		assert(res.find("Error") != std::string::npos);

		// Invalid base64 character
		std::string b64_args = "{\"data\": \"icA?\", \"format\": \"base64\"}";
		std::string b64_res = registry.execute_tool("x86_disassemble", b64_args, ctx);
		assert(b64_res.find("Error") != std::string::npos);
	}

	std::cout << "x86_disassemble tool verified successfully!" << std::endl;
	return 0;
}
