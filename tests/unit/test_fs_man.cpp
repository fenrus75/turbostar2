#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <zlib.h>
#include "../../src/agentlib/tool_context.h"
#include "../../src/tools/fs_man/fs_man.h"
#include "../../src/tools/fs_man/fs_man_search.h"

namespace fs = std::filesystem;

static void create_gzipped_file(const fs::path& path, const std::string& content) {
	gzFile file = gzopen(path.c_str(), "wb");
	assert(file != nullptr);
	int written = gzwrite(file, content.data(), content.size());
	assert(written == static_cast<int>(content.size()));
	gzclose(file);
}

static void write_text_file(const fs::path& path, const std::string& content) {
	std::ofstream out(path, std::ios::binary);
	assert(out.is_open());
	out << content;
}

int main() {
	test_watchdog::setup_watchdog(30);
	fs::path temp_dir = fs::temp_directory_path() / "turbostar_test_fs_man";
	fs::remove_all(temp_dir);
	fs::create_directories(temp_dir / "man1");
	fs::create_directories(temp_dir / "man3");

	// 1. Create standard mock man files
	// printf in section 1 (Command) as uncompressed text
	write_text_file(temp_dir / "man1/printf.1", ".SH NAME\nprintf - format and print data\n");
	
	// printf in section 3 (C Library) as gzipped text
	create_gzipped_file(temp_dir / "man3/printf.3.gz", ".SH NAME\nprintf - formatted output conversion\n");

	// calloc in section 3 (C Library) redirecting to malloc
	create_gzipped_file(temp_dir / "man3/calloc.3.gz", ".so man3/malloc.3");

	// malloc in section 3 (C Library) as gzipped text
	create_gzipped_file(temp_dir / "man3/malloc.3.gz", ".SH NAME\nmalloc - allocate memory\n");

	// demo in section 3 with a multi-section, multi-directive structure for filter testing.
	// Directives use .B so they render bold ("*Name=*") exactly like real systemd pages,
	// exercising the same code path the filter matches against.
	const std::string demo_content =
	    ".SH NAME\ndemo - demonstration page\n"
	    ".SH SYNOPSIS\ndemo(int x);\n"
	    ".SH OPTIONS\n"
	    ".TP\n.B \\-f\nflag option\n"
	    ".SH SECURITY\n"
	    ".TP\n.B ProtectKernelTunables=\nmake kernel tunables read-only\n"
	    ".TP\n.B ProtectHome=\nprotect home dir\n";
	create_gzipped_file(temp_dir / "man3/demo.3.gz", demo_content);

	// circular loop redirect files
	create_gzipped_file(temp_dir / "man3/loopA.3.gz", ".so man3/loopB.3");
	create_gzipped_file(temp_dir / "man3/loopB.3.gz", ".so man3/loopA.3");

	// deep redirect files (6 levels)
	create_gzipped_file(temp_dir / "man3/r1.3.gz", ".so man3/r2.3");
	create_gzipped_file(temp_dir / "man3/r2.3.gz", ".so man3/r3.3");
	create_gzipped_file(temp_dir / "man3/r3.3.gz", ".so man3/r4.3");
	create_gzipped_file(temp_dir / "man3/r4.3.gz", ".so man3/r5.3");
	create_gzipped_file(temp_dir / "man3/r5.3.gz", ".so man3/r6.3");
	create_gzipped_file(temp_dir / "man3/r6.3.gz", ".SH NAME\ndeep - final target\n");

	// Set env override to point fs_man to our mock temp directory
#ifdef _WIN32
	_putenv_s("TURBOSTAR_MAN_DIR_OVERRIDE", temp_dir.string().c_str());
#else
	setenv("TURBOSTAR_MAN_DIR_OVERRIDE", temp_dir.string().c_str(), 1);
#endif

	agentlib::tool_context ctx;

	// Test 1: Section priority (printf without section should prioritize C library section 3 over section 1)
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "printf"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("formatted output conversion") != std::string::npos);
		assert(result.find("format and print data") == std::string::npos);
		std::cout << "Test 1 passed: prioritized section 3 successfully.\n";
	}

	// Test 2: Explicit section override (printf with section 1)
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "printf"}, {"section", "1"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("format and print data") != std::string::npos);
		assert(result.find("formatted output conversion") == std::string::npos);
		std::cout << "Test 2 passed: explicit section override successfully.\n";
	}

	// Test 3: Follow redirect (calloc redirects to malloc)
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "calloc"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("allocate memory") != std::string::npos);
		std::cout << "Test 3 passed: redirect followed successfully.\n";
	}

	// Test 4: Circular redirect loop protection
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "loopA"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("Circular redirect loop") != std::string::npos);
		std::cout << "Test 4 passed: circular redirect loop detected.\n";
	}

	// Test 5: Deep redirect limit (exceeds depth 5)
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "r1"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("Maximum redirect depth") != std::string::npos);
		std::cout << "Test 5 passed: deep redirect depth limit enforced.\n";
	}

	// Test 6: Traversal characters rejection (validate_args fails)
	{
		tools::fs_man_validator validator;
		std::string error;
		
		nlohmann::json args1 = {{"name", "../passwd"}};
		bool valid1 = validator.validate_args(args1, ctx, error);
		assert(!valid1);
		assert(error.find("separators") != std::string::npos);

		nlohmann::json args2 = {{"name", "printf"}, {"section", "../1"}};
		bool valid2 = validator.validate_args(args2, ctx, error);
		assert(!valid2);
		assert(error.find("separators") != std::string::npos);

		std::cout << "Test 6 passed: traversal characters correctly rejected.\n";
	}

	// Test 7: output_path writes rendered markdown to a file and returns a short confirmation
	{
		// Grant write access to the temp dir so a filesystem output_path passes the security allowlist.
		ctx.fs_security.add_allowed_root(temp_dir, agentlib::access_type::write);

		tools::fs_man_validator validator;
		fs::path out_file = temp_dir / "printf_out.md";
		nlohmann::json args = {{"name", "printf"}, {"section", "3"}, {"output_path", out_file.string()}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		// Validation stores both output_path and safe_output_path for filesystem paths
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		// The tool returns a short success message instead of the content when output_path is set
		assert(result.find("Successfully wrote man page for printf to") != std::string::npos);
		assert(result.find("formatted output conversion") == std::string::npos);

		// The rendered markdown should have been written to the file
		assert(fs::exists(out_file));
		std::ifstream ifs(out_file, std::ios::binary);
		assert(ifs.is_open());
		std::string file_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		assert(file_content.find("formatted output conversion") != std::string::npos);
		std::cout << "Test 7 passed: output_path wrote markdown to file.\n";
	}

	// Test 8: filter by section (matches "# SECURITY" heading)
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "demo"}, {"section", "3"}, {"filter", "SECURITY"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("# SECURITY") != std::string::npos);
		assert(result.find("ProtectKernelTunables=") != std::string::npos);
		// Other sections should be excluded
		assert(result.find("# SYNOPSIS") == std::string::npos);
		assert(result.find("demo - demonstration page") == std::string::npos);
		std::cout << "Test 8 passed: filter by section (SECURITY) returned only that section.\n";
	}

	// Test 9: filter by directive (matches "*ProtectKernelTunables=*")
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "demo"}, {"section", "3"}, {"filter", "ProtectKernelTunables"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("ProtectKernelTunables=") != std::string::npos);
		// The directive's own body should be present but not the sibling directive
		assert(result.find("make kernel tunables read-only") != std::string::npos);
		assert(result.find("protect home dir") == std::string::npos);
		std::cout << "Test 9 passed: filter by directive (ProtectKernelTunables) returned only that directive.\n";
	}

	// Test 10: filter that matches nothing returns an error
	{
		tools::fs_man_validator validator;
		nlohmann::json args = {{"name", "demo"}, {"section", "3"}, {"filter", "NoSuchThing"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("did not match") != std::string::npos);
		std::cout << "Test 10 passed: filter with no match returns an error.\n";
	}

	// Test 11: fs_man_search on system man pages
	{
#ifdef _WIN32
		_putenv_s("TURBOSTAR_MAN_DIR_OVERRIDE", "");
#else
		unsetenv("TURBOSTAR_MAN_DIR_OVERRIDE");
#endif

		tools::fs_man_search_validator validator;
		nlohmann::json args = {{"query", "printf"}};
		std::string error;
		bool valid = validator.validate_args(args, ctx, error);
		assert(valid);
		auto tool = validator.create_tool(args);
		assert(tool != nullptr);

		std::string result = tool->execute(ctx);
		assert(result.find("printf") != std::string::npos);
		assert(result.find("| Page | Section |") != std::string::npos);
		std::cout << "Test 7 passed: fs_man_search found matches on system man pages.\n";
	}

	// Cleanup temp dir
	fs::remove_all(temp_dir);
	std::cout << "All fs_man tests passed!\n";
	return 0;
}
