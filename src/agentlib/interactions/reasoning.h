#pragma once
#include "base.h"

namespace agentlib {

class interaction_reasoning : public agent_interaction {
public:
    explicit interaction_reasoning(std::string text) : text_(std::move(text)) {
        set_boxed(true, 10, "Agent Reasoning"); // 10 is White on Green (current default)
    }
    
    void set_age(int age) override {
        agent_interaction::set_age(age);
        if (age > 5 && is_boxed()) {
            set_boxed(false);
        } else if (age <= 5 && !is_boxed()) {
            set_boxed(true, 10, "Agent Reasoning");
        }
    }
    
    std::string get_raw_text() const override { return "Thinking: " + text_; }
    void append_text(const std::string& t) { text_ += t; invalidate_cache(); }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib
