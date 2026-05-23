#pragma once

#include <string>
#include <vector>
#include <memory>

namespace agentlib {

struct interaction_line {
    std::string text;
    int color_pair;
};

class agent_interaction {
public:
    virtual ~agent_interaction() = default;

    int get_height(int width) const;
    const std::vector<interaction_line>& render(int width) const;
    virtual std::string get_raw_text() const = 0;

    void invalidate_cache() { cached_width_ = -1; }

protected:
    virtual std::vector<interaction_line> format_lines(int width) const = 0;

    // Helper for wrapping text with optional prefix
    static std::vector<interaction_line> wrap_text(const std::string& prefix, const std::string& text, int width, int color_pair);

private:
    mutable int cached_width_{-1};
    mutable std::vector<interaction_line> cached_lines_;
};

} // namespace agentlib
