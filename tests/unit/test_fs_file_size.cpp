#include <cassert>
#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
#include "../../src/agentlib/tool_context.h"
#include "../../src/tools/fs_file_size/fs_file_size.h"

namespace fs = std::filesystem;

int main() {
	fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_filesize";
	fs::create_directories(temp_dir);
	
	// Create test files
	fs::path regular_file = temp_dir / "test.txt";
	{
		std::ofstream out(regular_file);
		out << "Hello, World!\n";
		out << "This is a test file.\n";
		out.close();
	}

	fs::path empty_file = temp_dir / "empty.txt";
	{
		std::ofstream out(empty_file);
		out.close();
	}

	fs::path subdir = temp_dir / "subdir";
	fs::create_directories(subdir);

	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(temp_dir);
	ctx.fs_security.add_allowed_root(temp_dir, agentlib::access_type::read);

	// Test 1: Regular file with content
	{
		tools::fs_file_size_validator validator;
		nlohmann::json args = {{"path", "test.txt"}};
		
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid && "Should validate regular file path");
		
		auto tool = validator.create_tool(args);
		assert(tool != nullptr && "Tool should be created");
		
		std::string result = tool->execute(ctx);
		assert(result.find("bytes") != std::string::npos && "Result should contain 'bytes'");
		assert(result.find("Error") == std::string::npos && "Result should not contain error");
		
		// Verify the size is correct (40 bytes: "Hello, World!\nThis is a test file.\n")
		assert(result.find("40 bytes") != std::string::npos && "File size should be 40 bytes");
		
		std::cout << "Test 1 passed: Regular file size correctly reported\n";
	}

	// Test 2: Empty file
	{
		tools::fs_file_size_validator validator;
		nlohmann::json args = {{"path", "empty.txt"}};
		
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid && "Should validate empty file path");
		
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);
		
		std::string result = tool->execute(ctx);
		assert(result.find("0 bytes") != std::string::npos && "Empty file should be 0 bytes");
		assert(result.find("Error") == std::string::npos);
		
		std::cout << "Test 2 passed: Empty file size correctly reported\n";
	}

	// Test 3: Directory should fail (SECURITY CHECK)
	{
		tools::fs_file_size_validator validator;
		nlohmann::json args = {{"path", "subdir"}};
		
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid && "Directory path should pass validation (it's in allowed root)");
		
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);
		
		std::string result = tool->execute(ctx);
		assert(result.find("Error") != std::string::npos && "Directory access should fail");
		assert(result.find("not a regular file") != std::string::npos && "Error should mention 'not a regular file'");
		
		std::cout << "Test 3 passed: Directory correctly rejected as non-regular file\n";
	}

	// Test 4: Non-existent file
	{
		tools::fs_file_size_validator validator;
		nlohmann::json args = {{"path", "nonexistent.txt"}};
		
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid && "Non-existent file path should pass validation");
		
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);
		
		std::string result = tool->execute(ctx);
		assert(result.find("Error") != std::string::npos && "Non-existent file should fail");
		
		std::cout << "Test 4 passed: Non-existent file correctly reported error\n";
	}

	// Test 5: Relative path with subdirectory
	{
		tools::fs_file_size_validator validator;
		nlohmann::json args = {{"path", "subdir"}};
		
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);
		
		std::string result = tool->execute(ctx);
		assert(result.find("not a regular file") != std::string::npos);
		
		std::cout << "Test 5 passed: Relative path to directory correctly rejected\n";
	}

	// Test 6: Path traversal attempt (should be blocked by security manager)
	{
		tools::fs_file_size_validator validator;
		nlohmann::json args = {{"path", "../etc/passwd"}};
		
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(!valid && "Path traversal should be blocked");
		assert(error.find("outside") != std::string::npos || error.find("allowed") != std::string::npos);
		
		std::cout << "Test 6 passed: Path traversal attempt correctly blocked\n";
	}

	// Cleanup
	fs::remove_all(temp_dir);
	
	std::cout << "\nAll fs_file_size unit tests passed!\n";
	std::cout << "Security checks verified:\n";
	std::cout << "  - Regular files: OK\n";
	std::cout << "  - Empty files: OK\n";
	std::cout << "  - Directories rejected: OK\n";
	std::cout << "  - Non-existent files: OK\n";
	std::cout << "  - Path traversal blocked: OK\n";
	
	return 0;
}
