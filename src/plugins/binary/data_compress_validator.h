#pragma once

#include "agentlib/tool_validator.h"
#include "data_compress_tool.h"

namespace tools {

class data_compress_validator : public agentlib::tool_validator {
public:
	data_compress_validator() = default;
	~data_compress_validator() override = default;

	bool is_pure() const override { return false; }
	std::string get_name() const override { return "data_compress"; }
	std::string get_description() const override;
	std::string get_family() const override { return "binary|hexedit"; }
	nlohmann::json get_parameters_schema() const override;

protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
				std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &raw_json) const override;

private:
	mutable data_compress_args args_;
};

}
