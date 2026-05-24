#pragma once
#include "base.h"

namespace agentlib {

class interaction_user_message : public agent_interaction {
public:
    explicit interaction_user_message(std::string text) : text_(std::move(text)) {}
    interaction_type get_type() const override { return interaction_type::user_message; }
    interaction_role get_role() const override { return interaction_role::user; }
    std::string get_raw_text() const override { return "User: " + text_; }
    void append_text(const std::string& t) { text_ += t; invalidate_cache(); }

    bool can_merge_with_previous(const agent_interaction& previous) const override {
        return previous.get_type() == interaction_type::user_message;
    }
protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;

private:
    std::string text_;
};

} // namespace agentlib
