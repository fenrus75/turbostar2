// Tested source file: src/agentlib/tool_validator.cpp, src/agentlib/tool_registry.cpp
#include <cassert>
#include <iostream>
#include "agentlib/llm_types.h"
#include "agentlib/single_string_tool_validator.h"
#include "agentlib/tool_registry.h"
#include "test_watchdog.h"

using namespace agentlib;

class mock_tool : public llm_tool
{
      public:
	explicit mock_tool(std::string str) : str_(std::move(str))
	{
	}
	bool validate_runtime(const tool_context & /*ctx*/, std::string & /*out_error*/) const override
	{
		return true;
	}
	std::string execute(tool_context & /*ctx*/) override
	{
		return str_;
	}

      private:
	std::string str_;
};

class mock_validator : public single_string_tool_validator
{
      public:
	std::string get_name() const override
	{
		return "mock";
	}
	std::string get_description() const override
	{
		return "mock desc";
	}
	std::string get_parameter_name() const override
	{
		return "param";
	}
	std::string get_parameter_description() const override
	{
		return "param desc";
	}

	bool validate_string_arg(const std::string &arg, const tool_context & /*ctx*/, std::string & /*out_error*/) const override
	{
		if (arg == "fail")
			return false;
		return true;
	}

	std::unique_ptr<llm_tool> create_tool_from_string(const std::string &arg) const override
	{
		return std::make_unique<mock_tool>(arg);
	}
};

int main()
{
	test_watchdog::setup_watchdog(30);
	mock_validator validator;
	tool_context ctx;
	std::string error;

	nlohmann::json valid_args = {{"param", "success"}};
	nlohmann::json invalid_args = {{"param", "fail"}};

	// 1. Attempt to create tool WITHOUT validating. Must fail (return nullptr).
	auto tool1 = validator.create_tool(valid_args);
	assert(tool1 == nullptr && "Security invariant violated: tool created without validation!");

	// 2. Attempt to validate with failing args, then create tool. Must fail.
	bool valid = validator.validate_args(invalid_args, ctx, error);
	assert(!valid);
	auto tool2 = validator.create_tool(invalid_args);
	assert(tool2 == nullptr && "Security invariant violated: tool created after failed validation!");

	// 3. Attempt to validate with passing args, then create tool. Must succeed.
	valid = validator.validate_args(valid_args, ctx, error);
	assert(valid);
	auto tool3 = validator.create_tool(valid_args);
	assert(tool3 != nullptr && "Security invariant violated: valid tool not created!");

	// 4. Test normalize_tool_call with aliases
	{
		// Test Group 1: read_file -> fs_read_lines
		tool_call tc1;
		tc1.function.name = "read_file";
		tc1.function.arguments = R"({"file_path": "src/main.cpp", "start_line": 10, "end_line": 20})";
		normalize_tool_call(tc1);
		assert(tc1.function.name == "fs_read_lines");
		auto parsed1 = nlohmann::json::parse(tc1.function.arguments);
		assert(parsed1["path"] == "src/main.cpp");
		assert(parsed1["start_line"] == 10);
		assert(parsed1["end_line"] == 20);

		// Test Group 1b: fs_read_file -> fs_read_lines
		tool_call tc1b;
		tc1b.function.name = "fs_read_file";
		tc1b.function.arguments = R"({"path": "src/main.cpp", "start_line": 10, "end_line": 20})";
		normalize_tool_call(tc1b);
		assert(tc1b.function.name == "fs_read_lines");
		auto parsed1b = nlohmann::json::parse(tc1b.function.arguments);
		assert(parsed1b["path"] == "src/main.cpp");
		assert(parsed1b["start_line"] == 10);
		assert(parsed1b["end_line"] == 20);

		// Test Group 2: search_grep -> fs_grep_files
		tool_call tc2;
		tc2.function.name = "search_grep";
		tc2.function.arguments = R"({"query": "foo", "dir": "src/"})";
		normalize_tool_call(tc2);
		assert(tc2.function.name == "fs_grep_files");
		auto parsed2 = nlohmann::json::parse(tc2.function.arguments);
		assert(parsed2["pattern"] == "foo");
		// The directory arg must normalize to 'search_path' (the canonical name that
		// fs_grep_files_validator::validate_args_impl reads). Normalizing to 'dir_path'
		// would be silently dropped by the validator, causing the search to default to
		// the project root instead of the requested subdirectory.
		assert(parsed2["search_path"] == "src/");

		// Test Group 2b: grep alias with a 'path' directory arg -> fs_grep_files
		tool_call tc2b;
		tc2b.function.name = "grep";
		tc2b.function.arguments = R"({"pattern": "bar", "path": "src/lib/"})";
		normalize_tool_call(tc2b);
		assert(tc2b.function.name == "fs_grep_files");
		auto parsed2b = nlohmann::json::parse(tc2b.function.arguments);
		assert(parsed2b["pattern"] == "bar");
		assert(parsed2b["search_path"] == "src/lib/");

		// Test Group 2c: find_in_files alias with a 'dir_path' directory arg -> fs_grep_files
		tool_call tc2c;
		tc2c.function.name = "find_in_files";
		tc2c.function.arguments = R"({"text": "baz", "dir_path": "src/ui/"})";
		normalize_tool_call(tc2c);
		assert(tc2c.function.name == "fs_grep_files");
		auto parsed2c = nlohmann::json::parse(tc2c.function.arguments);
		assert(parsed2c["pattern"] == "baz");
		assert(parsed2c["search_path"] == "src/ui/");

		// Test Group 2d: grep_search alias with 'Query', 'SearchPath', 'CaseInsensitive', 'IsRegex' -> fs_grep_files
		tool_call tc2d;
		tc2d.function.name = "grep_search";
		tc2d.function.arguments =
		    R"({"Query": "my_pattern", "SearchPath": "src/", "CaseInsensitive": true, "IsRegex": true})";
		normalize_tool_call(tc2d);
		assert(tc2d.function.name == "fs_grep_files");
		auto parsed2d = nlohmann::json::parse(tc2d.function.arguments);
		assert(parsed2d["pattern"] == "my_pattern");
		assert(parsed2d["search_path"] == "src/");
		assert(parsed2d["case_insensitive"] == true);
		assert(parsed2d["is_regex"] == true);

		// Test Group 2e: grep_search alias with snake_case and Includes array
		tool_call tc2e;
		tc2e.function.name = "grep_search";
		tc2e.function.arguments = R"({"query": "another_pattern", "search_path": "tests/", "Includes": ["*.cpp"]})";
		normalize_tool_call(tc2e);
		assert(tc2e.function.name == "fs_grep_files");
		auto parsed2e = nlohmann::json::parse(tc2e.function.arguments);
		assert(parsed2e["pattern"] == "another_pattern");
		assert(parsed2e["search_path"] == "tests/");
		assert(parsed2e["include_ext"] == ".cpp");

		// Test Group 3: list_dir -> fs_list_dir
		tool_call tc3;
		tc3.function.name = "list_dir";
		tc3.function.arguments = R"({"directory": "src/ui/"})";
		normalize_tool_call(tc3);
		assert(tc3.function.name == "fs_list_dir");
		auto parsed3 = nlohmann::json::parse(tc3.function.arguments);
		assert(parsed3["path"] == "src/ui/");

		// Test Group 4: create_directory -> fs_mkdir
		tool_call tc4;
		tc4.function.name = "create_directory";
		tc4.function.arguments = R"({"dir_path": "tmp/dir"})";
		normalize_tool_call(tc4);
		assert(tc4.function.name == "fs_mkdir");
		auto parsed4 = nlohmann::json::parse(tc4.function.arguments);
		assert(parsed4["path"] == "tmp/dir");

		// Test Group 5: run_tests -> fs_run_tests
		tool_call tc5;
		tc5.function.name = "run_tests";
		tc5.function.arguments = "{}";
		normalize_tool_call(tc5);
		assert(tc5.function.name == "fs_run_tests");

		// Test Group 6: git_diff -> git_diff_unstaged
		tool_call tc6;
		tc6.function.name = "git_diff";
		tc6.function.arguments = R"({"file_path": "src/utf8.cpp"})";
		normalize_tool_call(tc6);
		assert(tc6.function.name == "git_diff_unstaged");
		auto parsed6 = nlohmann::json::parse(tc6.function.arguments);
		assert(parsed6["path"] == "src/utf8.cpp");
	}

	// 5. Test Tool Families
	{
		std::cout << "Running tool family tests..." << std::endl;
		tool_registry &registry = tool_registry::get_instance();

		class family_mock_validator : public single_string_tool_validator
		{
		      public:
			std::string get_name() const override
			{
				return "family_mock";
			}
			std::string get_description() const override
			{
				return "family mock desc";
			}
			std::string get_family() const override
			{
				return "test_family";
			}
			std::string get_parameter_name() const override
			{
				return "param";
			}
			std::string get_parameter_description() const override
			{
				return "param desc";
			}

			bool validate_string_arg(const std::string & /*arg*/, const tool_context & /*ctx*/,
						 std::string & /*out_error*/) const override
			{
				return true;
			}

			std::unique_ptr<llm_tool> create_tool_from_string(const std::string &arg) const override
			{
				return std::make_unique<mock_tool>(arg);
			}
		};

		registry.register_validator([]() { return std::make_unique<family_mock_validator>(); });

		// Test active families filtering in get_tools_json
		std::vector<std::string> active_families = {"base"};
		agent_properties props;
		props.active_families = active_families;
		nlohmann::json tools = registry.get_tools_json(true, props);
		bool found_family_mock = false;
		for (const auto &t : tools) {
			if (t["function"]["name"] == "family_mock") {
				found_family_mock = true;
			}
		}
		assert(!found_family_mock && "family_mock should NOT be in base family context!");

		active_families.emplace_back("test_family");
		props.active_families = active_families;
		tools = registry.get_tools_json(true, props);
		found_family_mock = false;
		for (const auto &t : tools) {
			if (t["function"]["name"] == "family_mock") {
				found_family_mock = true;
			}
		}
		assert(found_family_mock && "family_mock SHOULD be in context when test_family is active!");

		// Test validation and prepare_tool under inactive family
		tool_context family_ctx;
		family_ctx.properties.active_families = {"base"};
		bool is_active_called = false;
		family_ctx.is_family_active = [&](const std::string &fam) {
			is_active_called = true;
			return fam == "base"; // only base is active
		};

		auto prep_res = registry.prepare_tool("family_mock", R"({"param": "val"})", family_ctx);
		assert(is_active_called);
		assert(prep_res.tool == nullptr && "prepare_tool should fail for inactive family!");
		assert(prep_res.error_message.find("not active") != std::string::npos);

		// Test validation and prepare_tool under active family
		is_active_called = false;
		family_ctx.properties.active_families = {"base", "test_family"};
		family_ctx.is_family_active = [&](const std::string &fam) {
			is_active_called = true;
			return fam == "base" || fam == "test_family";
		};

		prep_res = registry.prepare_tool("family_mock", R"({"param": "val"})", family_ctx);
		assert(is_active_called);
		assert(prep_res.tool != nullptr && "prepare_tool should succeed for active family!");
		assert(prep_res.error_message.empty());

		// Clean up
		registry.unregister_validator("family_mock");
		std::cout << "Tool family tests passed!\n";
	}

	// 6. Test tool_validator parameter aliases (length, limit, test_names, path)
	{
		std::cout << "Testing tool_validator parameter alias normalization..." << std::endl;
		class test_alias_validator : public tool_validator
		{
		      public:
			std::string get_name() const override
			{
				return "test_alias";
			}
			std::string get_description() const override
			{
				return "test alias";
			}
			nlohmann::json get_parameters_schema() const override
			{
				return {{"type", "object"},
					{"properties",
					 {{"path", {{"type", "string"}}},
					  {"length", {{"type", "integer"}}},
					  {"limit", {{"type", "integer"}}},
					  {"test_names", {{"type", "array"}}}}}};
			}

		      protected:
			bool validate_args_impl(const nlohmann::json & /*args*/, const tool_context & /*ctx*/,
						std::string & /*out_error*/) const override
			{
				return true;
			}
			std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
			{
				return nullptr;
			}
		};

		test_alias_validator v;
		tool_context vctx;
		std::string verr;

		// length aliases
		for (const auto &alias : {"lines", "line_count", "num_lines", "lines_count", "num_items", "max_lines"}) {
			nlohmann::json a = {{alias, 42}};
			assert(v.validate_args(a, vctx, verr));
			assert(v.get_validated_args()["length"] == 42);
			assert(!v.get_validated_args().contains(alias));
		}

		// limit aliases
		for (const auto &alias : {"count", "max_results", "max_count", "n", "max_commits", "max_items"}) {
			nlohmann::json a = {{alias, 10}};
			assert(v.validate_args(a, vctx, verr));
			assert(v.get_validated_args()["limit"] == 10);
			assert(!v.get_validated_args().contains(alias));
		}

		// test_names aliases
		for (const auto &alias : {"test_name", "test", "tests"}) {
			nlohmann::json a = {{alias, "unit_test_foo"}};
			assert(v.validate_args(a, vctx, verr));
			assert(v.get_validated_args()["test_names"] == nlohmann::json::array({"unit_test_foo"}));
			assert(!v.get_validated_args().contains(alias));
		}

		// path aliases (including PascalCase / camelCase / external harness names)
		for (const auto &alias : {"file_path", "filepath", "filename", "file", "target_file", "root_dir", "dir", "directory",
					  "AbsolutePath", "TargetFile", "filePath", "DirectoryPath", "SearchDirectory"}) {
			nlohmann::json a = {{alias, "src/main.cpp"}};
			assert(v.validate_args(a, vctx, verr));
			assert(v.get_validated_args()["path"] == "src/main.cpp");
			assert(!v.get_validated_args().contains(alias));
		}

		// Test cross-harness casing & vocabulary normalization (Antigravity PascalCase & Copilot camelCase)
		class test_cross_harness_validator : public tool_validator
		{
		      public:
			std::string get_name() const override
			{
				return "test_cross_harness";
			}
			std::string get_description() const override
			{
				return "test cross harness";
			}
			nlohmann::json get_parameters_schema() const override
			{
				return {{"type", "object"},
					{"properties",
					 {{"path", {{"type", "string"}}},
					  {"start_line", {{"type", "integer"}}},
					  {"end_line", {{"type", "integer"}}},
					  {"command", {{"type", "string"}}},
					  {"content", {{"type", "string"}}},
					  {"force_overwrite", {{"type", "boolean"}}},
					  {"target_content", {{"type", "string"}}},
					  {"replacement_content", {{"type", "string"}}},
					  {"chunks",
					   {{"type", "array"},
					    {"items",
					     {{"type", "object"},
					      {"properties",
					       {{"target_content", {{"type", "string"}}},
						{"replacement_content", {{"type", "string"}}},
						{"start_line", {{"type", "integer"}}},
						{"end_line", {{"type", "integer"}}}}}}}}}}}};
			}

		      protected:
			bool validate_args_impl(const nlohmann::json & /*args*/, const tool_context & /*ctx*/,
						std::string & /*out_error*/) const override
			{
				return true;
			}
			std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
			{
				return nullptr;
			}
		};

		test_cross_harness_validator chv;

		// Antigravity style (PascalCase + vocabulary aliases)
		nlohmann::json agy_args = {{"AbsolutePath", "src/utf8.cpp"},
					   {"StartLine", 10},
					   {"EndLine", 25},
					   {"CommandLine", "meson test -C build"},
					   {"CodeContent", "int main() {}"},
					   {"Overwrite", true},
					   {"TargetContent", "old_code"},
					   {"ReplacementContent", "new_code"},
					   {"ReplacementChunks",
					    nlohmann::json::array({{{"TargetContent", "c_old"},
								    {"ReplacementContent", "c_new"},
								    {"StartLine", 1},
								    {"EndLine", 5}}})}};
		assert(chv.validate_args(agy_args, vctx, verr));
		const auto &v_agy = chv.get_validated_args();
		assert(v_agy["path"] == "src/utf8.cpp");
		assert(v_agy["start_line"] == 10);
		assert(v_agy["end_line"] == 25);
		assert(v_agy["command"] == "meson test -C build");
		assert(v_agy["content"] == "int main() {}");
		assert(v_agy["force_overwrite"] == true);
		assert(v_agy["target_content"] == "old_code");
		assert(v_agy["replacement_content"] == "new_code");
		assert(v_agy["chunks"].size() == 1);
		assert(v_agy["chunks"][0]["target_content"] == "c_old");
		assert(v_agy["chunks"][0]["replacement_content"] == "c_new");
		assert(v_agy["chunks"][0]["start_line"] == 1);
		assert(v_agy["chunks"][0]["end_line"] == 5);

		// Copilot style (camelCase + vocabulary aliases)
		nlohmann::json copilot_args = {{"filePath", "src/utf8.h"},
					       {"startLine", 5},
					       {"endLine", 15},
					       {"commandLine", "ninja -C build"},
					       {"oldString", "foo()"},
					       {"newString", "bar()"},
					       {"edits",
						nlohmann::json::array(
						    {{{"oldString", "x"}, {"newString", "y"}, {"startLine", 2}, {"endLine", 4}}})}};
		assert(chv.validate_args(copilot_args, vctx, verr));
		const auto &v_cop = chv.get_validated_args();
		assert(v_cop["path"] == "src/utf8.h");
		assert(v_cop["start_line"] == 5);
		assert(v_cop["end_line"] == 15);
		assert(v_cop["command"] == "ninja -C build");
		assert(v_cop["target_content"] == "foo()");
		assert(v_cop["replacement_content"] == "bar()");
		assert(v_cop["chunks"].size() == 1);
		assert(v_cop["chunks"][0]["target_content"] == "x");
		assert(v_cop["chunks"][0]["replacement_content"] == "y");
		assert(v_cop["chunks"][0]["start_line"] == 2);
		assert(v_cop["chunks"][0]["end_line"] == 4);
	}

	std::cout << "Tool infrastructure NVI invariant tests passed!\n";
	return 0;
}
