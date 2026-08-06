#include "test_watchdog.h"
#include "agentlib/ai_agent.h"
#include "agentlib/virtual_file_system.h"
#include "agentlib/skill_manager.h"
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

	// 6. Test skills.md and diagnostics.md VFS endpoints
	agentlib::skill_manager::get_instance().register_skill("demo_skill", "Demo Skill Description", "skills://demo_skill/", true);
	auto skills_doc = vfs.read_file("system://skills.md");
	assert(skills_doc.has_value());
	std::string skills_text = std::string((*skills_doc)->view());
	std::cout << "\nsystem://skills.md content:\n" << skills_text << std::endl;
	assert(skills_text.find("Available Agent Skills") != std::string::npos);
	assert(skills_text.find("demo_skill") != std::string::npos);

	auto diag_doc = vfs.read_file("system://project/diagnostics.md");
	assert(diag_doc.has_value());
	std::string diag_text = std::string((*diag_doc)->view());
	std::cout << "\nsystem://project/diagnostics.md content:\n" << diag_text << std::endl;
	assert(diag_text.find("No compilation errors or warnings found.") != std::string::npos ||
	       diag_text.find("Workspace Compilation Diagnostics") != std::string::npos);

	auto diag_alias = vfs.read_file("system://compile_summary.md");
	assert(diag_alias.has_value());
	assert(std::string((*diag_alias)->view()) == diag_text);

	auto proj_info_doc = vfs.read_file("system://project/info.md");
	assert(proj_info_doc.has_value());
	std::string proj_info_text = std::string((*proj_info_doc)->view());
	std::cout << "\nsystem://project/info.md content:\n" << proj_info_text << std::endl;
	assert(proj_info_text.find("Project Workspace Overview") != std::string::npos);
	assert(proj_info_text.find("Upstream Repository") != std::string::npos);
	assert(proj_info_text.find("Git Branch") != std::string::npos);
	assert(proj_info_text.find("Build System") != std::string::npos);

	// Test system://project/testlist.md endpoint (lists available test names).
	// This tolerates both an empty and a populated list (depends on whether the build
	// directory / test listing is configured), but must always resolve and produce a header.
	assert(vfs.exists("system://project/testlist.md"));
	auto testlist_doc = vfs.read_file("system://project/testlist.md");
	assert(testlist_doc.has_value());
	std::string testlist_text = std::string((*testlist_doc)->view());
	std::cout << "\nsystem://project/testlist.md content:\n" << testlist_text << std::endl;
	assert(testlist_text.find("# Available Tests") != std::string::npos);

	// Alias resolution for testlist must work too.
	auto testlist_alias = vfs.read_file("system://project/test-names.md");
	assert(testlist_alias.has_value());
	assert(std::string((*testlist_alias)->view()) == testlist_text);

	// The project/ directory listing must include testlist.md.
	std::vector<std::string> proj_listing;
	auto proj_dir_list = vfs.list_directory("system://project/");
	for (const auto &item : proj_dir_list) {
		std::cout << "  - project item: " << item.uri << " [" << item.details << "]" << std::endl;
		proj_listing.push_back(item.uri);
	}
	bool found_testlist = false;
	for (const auto &u : proj_listing) {
		if (u == "system://project/testlist.md") {
			found_testlist = true;
		}
	}
	assert(found_testlist);

	// The ?search=<pattern> filter must be honored (case-insensitive substring).
	// When the list is non-empty, the results must be a strict subset; when empty, the
	// well-formed "no tests matching" message is returned.
	auto testlist_filter_doc = vfs.read_file("system://project/testlist.md?search=unit_");
	assert(testlist_filter_doc.has_value());
	std::string testlist_filter_text = std::string((*testlist_filter_doc)->view());
	std::cout << "\nsystem://project/testlist.md?search=unit_ content:\n" << testlist_filter_text << std::endl;
	assert(testlist_filter_text.find("# Available Tests") != std::string::npos);
	if (testlist_filter_text.find("No tests matching") == std::string::npos) {
		// Non-empty filtered result: must not contain the full unfiltered header count mismatch,
		// and every row must contain the filter substring "unit_".
		size_t pos = testlist_filter_text.find("| ");
		while (pos != std::string::npos && testlist_filter_text.substr(pos, 2) == "| ") {
			size_t nl = testlist_filter_text.find('\n', pos);
			std::string row = testlist_filter_text.substr(pos, (nl == std::string::npos ? std::string::npos : nl - pos));
			if (row.find(":---") == std::string::npos && row.find("Test Name") == std::string::npos) {
				assert(row.find("unit_") != std::string::npos);
			}
			pos = (nl == std::string::npos) ? std::string::npos : testlist_filter_text.find("| ", nl);
		}
	}

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

	// Test system://subagents/ endpoints
	auto test_subagent = ai_agent::create(999, "research", nullptr, nullptr, nullptr);
	test_subagent->set_final_result("Research findings: VFS subagents implementation is operational.");

	auto subagents_idx_doc = vfs.read_file("system://subagents.md");
	assert(subagents_idx_doc.has_value());
	std::string subagents_idx_text = std::string((*subagents_idx_doc)->view());
	std::cout << "\nsystem://subagents.md content:\n" << subagents_idx_text << std::endl;
	assert(subagents_idx_text.find("Active & Recent Subagents") != std::string::npos);
	assert(subagents_idx_text.find("999") != std::string::npos);

	auto subagent_summary_doc = vfs.read_file("system://subagents/999.md");
	assert(subagent_summary_doc.has_value());
	std::string subagent_summary_text = std::string((*subagent_summary_doc)->view());
	std::cout << "\nsystem://subagents/999.md content:\n" << subagent_summary_text << std::endl;
	assert(subagent_summary_text.find("Subagent 999") != std::string::npos);
	assert(subagent_summary_text.find("Has Final Result") != std::string::npos);

	auto subagent_fr_doc = vfs.read_file("system://subagents/999/final_result.md");
	assert(subagent_fr_doc.has_value());
	std::string subagent_fr_text = std::string((*subagent_fr_doc)->view());
	std::cout << "\nsystem://subagents/999/final_result.md content:\n" << subagent_fr_text << std::endl;
	assert(subagent_fr_text.find("VFS subagents implementation is operational") != std::string::npos);

	auto subagent_tr_doc = vfs.read_file("system://subagents/999/transcript.md");
	assert(subagent_tr_doc.has_value());
	std::string subagent_tr_text = std::string((*subagent_tr_doc)->view());
	std::cout << "\nsystem://subagents/999/transcript.md content:\n" << subagent_tr_text << std::endl;
	assert(subagent_tr_text.find("Execution Transcript for Subagent 999") != std::string::npos);

	auto subagent_dir_list = vfs.list_directory("system://subagents/");
	assert(!subagent_dir_list.empty());
	bool found_sub999_dir = false;
	for (const auto &item : subagent_dir_list) {
		std::cout << "  - Subagent dir item: " << item.uri << " [" << item.details << "]" << std::endl;
		if (item.uri == "system://subagents/999.md" || item.uri == "system://subagents/999/") {
			found_sub999_dir = true;
		}
	}
	assert(found_sub999_dir);

	std::cout << "test_system_vfs passed successfully!" << std::endl;
	return 0;
}
