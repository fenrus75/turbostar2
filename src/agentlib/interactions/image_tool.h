#pragma once
#include "base.h"

namespace agentlib {

class interaction_image_tool : public agent_interaction {
public:
    interaction_image_tool(std::string tool_name, std::string call_text, std::string src_uri = "");
    ~interaction_image_tool() override = default;

    interaction_type get_type() const override { return interaction_type::tool_call; }
    interaction_role get_role() const override { return interaction_role::thinking; }
    std::string get_raw_text() const override;
    std::string get_grouping_key() const override { return tool_name_; }

    void set_result(std::string result_text);
    void set_output_image(std::string dst_uri);

    const std::string& get_call_text() const { return call_text_; }
    const std::string& get_result_text() const { return result_text_; }

protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;

private:
    std::string tool_name_;
    std::string call_text_;
    std::string result_text_;
    std::string src_uri_;
    std::string dst_uri_;
};

} // namespace agentlib
