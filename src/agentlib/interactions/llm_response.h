#pragma once
#include "base.h"

namespace agentlib {

class interaction_llm_response : public agent_interaction {
public:
    explicit interaction_llm_response(std::string text) : text_(std::move(text)) {}
    std::string get_raw_text() const override { return "LLM: " + text_; }
    void append_text(const std::string& t) { text_ += t; invalidate_cache(); }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib
