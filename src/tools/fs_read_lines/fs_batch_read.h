#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "agentlib/llm_tool.h"

namespace tools
{

enum class batch_item_type { lines, symbol, class_context };

struct batch_read_item {
	std::string requested_path;
	std::string safe_path;
	batch_item_type type = batch_item_type::lines;

	// For type == lines
	int start_line = 1;
	int end_line = 1000000;
	std::optional<int> tail;
	std::optional<int> length;

	// For type == symbol or class_context
	std::string name;

	// Error state if item validation failed
	bool has_error = false;
	std::string error_message;
};

struct fs_batch_read_args {
	std::vector<batch_read_item> items;
};

class fs_batch_read_tool : public agentlib::llm_tool
{
      public:
	explicit fs_batch_read_tool(fs_batch_read_args args);

	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	fs_batch_read_args args_;
	std::shared_ptr<agentlib::agent_interaction> interaction_;

	std::string execute_item(const batch_read_item &item, agentlib::tool_context &ctx, bool is_single_item);
	std::string execute_symbol(const batch_read_item &item, agentlib::tool_context &ctx);
	std::string execute_class_context(const batch_read_item &item, agentlib::tool_context &ctx);
};

} // namespace tools
