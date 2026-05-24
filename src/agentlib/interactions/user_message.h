#pragma once
#include "base.h"

namespace agentlib {

class interaction_user_message : public agent_interaction {
public:
    explicit interaction_user_message(std::string text) : text_(std::move(text)) {}
    interaction_type get_type() const override { return interaction_type::user_message; }
    std::string get_raw_text() const override { return "User: " + text_; }
    void append_text(const std::string& t) { text_ += t; invalidate_cache(); }

    bool can_merge_with_previous(const agent_interaction& previous) const override {
        return dynamic_cast<const interaction_user_message*>(&previous) != nullptr;
    }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib
