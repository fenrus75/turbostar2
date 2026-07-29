#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools {

struct data_compress_args {
	std::string input_data;
	std::string path;
	std::string input_file;
	std::string format;
	std::string output_format;
	std::string output_path;
	std::string output_file;
};

class data_compress_tool : public agentlib::llm_tool_action {
public:
	explicit data_compress_tool(data_compress_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

private:
	data_compress_args args_;
};

}
