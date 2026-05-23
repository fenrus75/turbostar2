#pragma once
#include "base.h"

namespace agentlib {

class interaction_terminal : public agent_interaction {
public:
    explicit interaction_terminal(std::string title, std::string text = "");
    
    void append_text(const std::string& t);
    void set_text(const std::string& t);
    
    std::string get_raw_text() const override;

protected:
    std::vector<interaction_line> format_lines(int width) const override;

private:
    std::string title_;
    std::string text_;
};

} // namespace agentlib
