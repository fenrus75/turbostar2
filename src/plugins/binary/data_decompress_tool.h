#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools {

struct data_decompress_args {
	std::string input_data;
	std::string format;
	std::string output_format;
	std::string output_file;
	size_t offset{0};
	long long length{-1};
};

class data_decompress_tool : public agentlib::llm_tool_action {
public:
	explicit data_decompress_tool(data_decompress_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

private:
	data_decompress_args args_;
};

}
