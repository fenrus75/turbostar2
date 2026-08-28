#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace agentlib
{
class virtual_file_system;
class document_snapshot;
} // namespace agentlib

namespace tools
{

struct dir_entry_metadata {
	std::string filename;
	char type = 'U'; // 'F' (File), 'D' (Directory), 'L' (Symlink), 'U' (Unknown)
	std::string size_bytes;
	std::string size_lines;
	std::string permissions; // e.g. "R--", "RWX", "R-X"
	std::string details;	 // libmagic output (if rich_metadata enabled)
};

struct list_dir_result {
	bool success = false;
	std::string error_message;
	std::vector<dir_entry_metadata> entries;
	std::string directory_name;
	int offset = 0;
	int limit = 100;
	size_t total_items = 0;
};

struct fs_list_dir_args {
	std::string path;
	bool rich_metadata = false;
	int limit = 100;
	int offset = 0;
};

class fs_list_dir_tool : public agentlib::llm_tool_action
{
      public:
	explicit fs_list_dir_tool(fs_list_dir_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	fs_list_dir_args args_;

	list_dir_result scan_vfs(agentlib::virtual_file_system *vfs, const std::string &path) const;
	list_dir_result scan_local_disk(const std::string &path, agentlib::tool_context &ctx) const;
	std::string format_entries_table(const list_dir_result &result) const;
};

class fs_list_dir_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "fs_list_dir";
	}
	std::string get_description() const override
	{
		return "Lists the contents of a directory as a Markdown table (Type, Size, Lines, Permissions, and optional rich metadata). ALWAYS use this tool to list directory contents instead of running 'ls' in a shell command.";;
	}
	nlohmann::json get_parameters_schema() const override;

	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable fs_list_dir_args args_;
};

} // namespace tools
