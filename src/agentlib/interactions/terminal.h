#pragma once
#include "base.h"

namespace agentlib {

class interaction_terminal : public agent_interaction {
public:
    explicit interaction_terminal(std::string title, std::string text = "");
    
    void append_text(const std::string& t);
    void set_text(const std::string& t);
    
    std::string get_raw_text() const override;
    interaction_type get_type() const override;
    interaction_role get_role() const override;

    bool needs_subpanel_header() const override { return true; }
    std::string get_subpanel_label() const override { return title_; }

protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;

private:
    std::string title_;
    std::string text_;
};

} // namespace agentlib
