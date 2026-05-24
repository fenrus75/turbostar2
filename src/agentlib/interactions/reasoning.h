#pragma once
#include "base.h"

namespace agentlib {

class interaction_reasoning : public agent_interaction {
public:
    explicit interaction_reasoning(std::string text) : text_(std::move(text)) {}
    
    interaction_type get_type() const override { return interaction_type::reasoning; }
    interaction_role get_role() const override { return interaction_role::thinking; }

    void set_age(int age) override {
        agent_interaction::set_age(age);
    }
    
    std::string get_raw_text() const override { return "Thinking: " + text_; }
    void append_text(const std::string& t) { text_ += t; invalidate_cache(); }

    bool needs_subpanel_header() const override { return true; }
    std::string get_subpanel_label() const override { return "Agent Reasoning"; }
protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;
private:
    std::string text_;
};

} // namespace agentlib
