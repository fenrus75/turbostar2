#include "test_watchdog.h"
#include "agentlib/virtual_file_system.h"
#include "agentlib/tool_registry.h"
#include "vfs/system_vfs_provider.h"
#include <cassert>
#include <iostream>

using namespace agentlib;

class test_system_vfs_validator : public tool_validator {
public:
	std::string get_name() const override { return "fs_read_lines"; }
	std::string get_description() const override { return "Reads lines from a file"; }
	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"path", {{"type", "string"}, {"description", "Path to file"}}}
			}},
			{"required", nlohmann::json::array({"path"})}
		};
	}
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override {
		return true;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override {
		return nullptr;
	}
};

REGISTER_TOOL(test_system_vfs_validator)

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing system_vfs_provider..." << std::endl;

	virtual_file_system vfs;

	// 1. Test static document existence & read
	assert(vfs.exists("system://languages/cpp23.md"));
	assert(vfs.exists("system://languages/c17.md"));
	assert(vfs.exists("system://languages/python311.md"));
	assert(vfs.exists("system://languages/rust2021.md"));
	assert(vfs.exists("system://languages/typescript.md"));
	assert(vfs.exists("system://languages/verilog.md"));
	assert(vfs.exists("system://workflows/code_review.md"));
	assert(vfs.exists("system://workflows/crash_analysis.md"));

	auto cpp_doc = vfs.read_file("system://languages/cpp23.md");
	assert(cpp_doc.has_value());
	std::string cpp_text = std::string((*cpp_doc)->view());
	std::cout << "[system://languages/cpp23.md content snippet]:\n" << cpp_text.substr(0, 100) << "...\n" << std::endl;
	assert(cpp_text.find("C++23 Development Guidelines") != std::string::npos);

	// 2. Test fallback alias URIs
	assert(vfs.exists("system://cpp23.md"));
	assert(vfs.exists("system://c.md"));
	assert(vfs.exists("system://rust.md"));
	assert(vfs.exists("system://ts.md"));
	assert(vfs.exists("system://verilog.md"));
	auto alias_doc = vfs.read_file("system://cpp23.md");
	assert(alias_doc.has_value());
	assert(std::string((*alias_doc)->view()) == cpp_text);

	// 3. Test dynamic generators
	assert(vfs.exists("system://agents.md"));
	assert(vfs.exists("system://tools.md"));
	assert(vfs.exists("system://tools_detailed.md"));
	assert(vfs.exists("system://tools/details.md"));
	assert(vfs.exists("system://mcp.md"));

	auto agents_doc = vfs.read_file("system://agents.md");
	assert(agents_doc.has_value());
	std::string agents_text = std::string((*agents_doc)->view());
	std::cout << "[system://agents.md snippet]:\n" << agents_text.substr(0, 120) << "...\n" << std::endl;
	assert(agents_text.find("# Available Subagents") != std::string::npos);

	auto tools_doc = vfs.read_file("system://tools.md");
	assert(tools_doc.has_value());
	std::string tools_text = std::string((*tools_doc)->view());
	assert(tools_text.find("# Registered System Tools") != std::string::npos);
	assert(tools_text.find("system://tools_detailed.md") != std::string::npos);
	assert(tools_text.find("?search=<pattern>") != std::string::npos);

	auto tools_detailed_doc = vfs.read_file("system://tools_detailed.md");
	assert(tools_detailed_doc.has_value());
	std::string tools_detailed_text = std::string((*tools_detailed_doc)->view());
	assert(tools_detailed_text.find("# Detailed System Tool Schemas") != std::string::npos);
	assert(tools_detailed_text.find("* **Description:**") != std::string::npos);

	auto tools_alias_doc = vfs.read_file("system://tools/details.md");
	assert(tools_alias_doc.has_value());
	assert(std::string((*tools_alias_doc)->view()) == tools_detailed_text);

	auto tools_search_doc = vfs.read_file("system://tools.md?search=fs_read_lines");
	assert(tools_search_doc.has_value());
	std::string search_text = std::string((*tools_search_doc)->view());
	assert(search_text.find("fs_read_lines") != std::string::npos);

	// 4. Test directory listing and purpose descriptions
	auto info_cpp = vfs.get_file_info("system://languages/cpp23.md");
	assert(info_cpp.has_value());
	assert(!info_cpp->details.empty());
	assert(info_cpp->details.find("Read when writing or refactoring C++23 code.") != std::string::npos);

	auto root_list = vfs.list_directory("system://");
	assert(!root_list.empty());
	std::cout << "\nsystem:// directory listing count: " << root_list.size() << std::endl;
	bool found_lang_dir = false;
	bool found_wf_dir = false;
	for (const auto &item : root_list) {
		std::cout << "  - " << item.uri << " (type: " << item.type << ") [" << item.details << "]" << std::endl;
		assert(!item.details.empty());
		if (item.uri == "system://languages/" && item.type == 'D') {
			found_lang_dir = true;
		}
		if (item.uri == "system://workflows/" && item.type == 'D') {
			found_wf_dir = true;
		}
	}
	assert(found_lang_dir);
	assert(found_wf_dir);

	auto lang_list = vfs.list_directory("system://languages/");
	assert(!lang_list.empty());
	assert(lang_list.size() >= 2);
	for (const auto &item : lang_list) {
		assert(item.type == 'F');
	}

	// 5. Test tool-families VFS space
	auto families_doc = vfs.read_file("system://tool-families.md");
	assert(families_doc.has_value());
	std::string families_text = std::string((*families_doc)->view());
	std::cout << "\nsystem://tool-families.md content:\n" << families_text << std::endl;
	assert(families_text.find("Tool Families Overview") != std::string::npos);
	assert(families_text.find("git") != std::string::npos);
	assert(families_text.find("base") != std::string::npos);

	auto git_fam_doc = vfs.read_file("system://tool-families/git.md");
	assert(git_fam_doc.has_value());
	std::string git_fam_text = std::string((*git_fam_doc)->view());
	std::cout << "\nsystem://tool-families/git.md content:\n" << git_fam_text << std::endl;
	assert(git_fam_text.find("Tool Family: `git`") != std::string::npos);
	assert(git_fam_text.find("git_status") != std::string::npos);

	auto fam_list = vfs.list_directory("system://tool-families/");
	assert(!fam_list.empty());
	bool found_git_fam = false;
	for (const auto &item : fam_list) {
		std::cout << "  - Family item: " << item.uri << " [" << item.details << "]" << std::endl;
		if (item.uri == "system://tool-families/git.md") {
			found_git_fam = true;
			assert(!item.details.empty());
		}
	}
	assert(found_git_fam);

	std::cout << "test_system_vfs passed successfully!" << std::endl;
	return 0;
}
