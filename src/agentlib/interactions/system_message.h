#pragma once
#include "base.h"

namespace agentlib {

class interaction_system_message : public agent_interaction {
public:
    explicit interaction_system_message(std::string text) : text_(std::move(text)) {
        set_boxed(true, 2, "System Context"); // 2 is Red on White/Gray
    }
    std::string get_raw_text() const override { return "System: " + text_; }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib
