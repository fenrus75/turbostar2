#pragma once
#include "base.h"

namespace agentlib {

class interaction_action : public agent_interaction {
public:
    enum class status { pending, success, failure };

    explicit interaction_action(std::string action_text);

    interaction_type get_type() const override { return interaction_type::action; }
    interaction_role get_role() const override { return interaction_role::agent; }

    void set_status(status s, const std::string& result_message = "");
    void set_action_text(const std::string& text);

    std::string get_raw_text() const override;

protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;
    
    // Allows subclasses to append additional content (like diffs or tables) below the status line
    virtual std::vector<interaction_line> format_extra_lines(int width, background_mode bg) const { 
        (void)width; // Unused in base
        (void)bg;
        return {}; 
    }

private:
    status status_ = status::pending;
    std::string action_text_;
    std::string result_text_;
};

} // namespace agentlib
