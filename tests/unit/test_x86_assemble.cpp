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

	std::cout << "Testing x86_assemble tool..." << std::endl;

	// 1. Verify family security restrictions
	bool x86_active = false;
	ctx.is_family_active = [&](const std::string &fam) {
		if (fam == "x86") {
			return x86_active;
		}
		return true;
	};

	std::string valid_args = "{\"instruction\": \"mov eax, eax\"}";
	{
		auto prep = registry.prepare_tool("x86_assemble", valid_args, ctx);
		assert(prep.tool == nullptr && "Should fail prepare when x86 family is inactive");
		assert(prep.error_message.find("not active") != std::string::npos);
	}

	x86_active = true;
	{
		auto prep = registry.prepare_tool("x86_assemble", valid_args, ctx);
		assert(prep.tool != nullptr && "Should succeed prepare when x86 family is active");
		assert(prep.error_message.empty());
	}

	// 2. Test 64-bit Intel assembly
	{
		std::string args = "{\"instruction\": \"mov eax, eax\", \"mode\": \"64\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_assemble", args, ctx);
		std::cout << "64-bit Intel Assembly Result: " << res << std::endl;
		assert(res == "89 c0");
	}

	// 3. Test 64-bit AT&T assembly
	{
		std::string args = "{\"instruction\": \"mov %eax, %eax\", \"mode\": \"64\", \"syntax\": \"att\"}";
		std::string res = registry.execute_tool("x86_assemble", args, ctx);
		std::cout << "64-bit AT&T Assembly Result: " << res << std::endl;
		assert(res == "89 c0");
	}

	// 4. Test 32-bit assembly
	{
		std::string args = "{\"instruction\": \"mov eax, eax\", \"mode\": \"32\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_assemble", args, ctx);
		std::cout << "32-bit Intel Assembly Result: " << res << std::endl;
		assert(res == "89 c0");
	}

	// 5. Test 16-bit assembly
	{
		std::string args = "{\"instruction\": \"mov ax, ax\", \"mode\": \"16\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_assemble", args, ctx);
		std::cout << "16-bit Intel Assembly Result: " << res << std::endl;
		assert(res == "89 c0");
	}

	// 6. Test invalid instruction assembly
	{
		std::string args = "{\"instruction\": \"invalid_instruction_here\", \"mode\": \"64\", \"syntax\": \"intel\"}";
		std::string res = registry.execute_tool("x86_assemble", args, ctx);
		std::cout << "Invalid instruction Result: " << res << std::endl;
		assert(res.find("Error") != std::string::npos);
	}

	std::cout << "x86_assemble tool verified successfully!" << std::endl;
	return 0;
}
