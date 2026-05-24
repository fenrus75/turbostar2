#pragma once
#include "base.h"

namespace agentlib {

class interaction_system_message : public agent_interaction {
public:
    explicit interaction_system_message(std::string text) : text_(std::move(text)) {
        set_boxed(true, 2, "System Context"); // 2 is Red on White/Gray
    }
    interaction_type get_type() const override { return interaction_type::system_message; }
    std::string get_raw_text() const override { return "System: " + text_; }

    bool can_merge_with_previous(const agent_interaction& previous) const override {
        return dynamic_cast<const interaction_system_message*>(&previous) != nullptr;
    }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib
