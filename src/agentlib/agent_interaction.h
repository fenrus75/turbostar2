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

protected:
    virtual std::vector<interaction_line> format_lines(int width) const = 0;

private:
    mutable int cached_width_{-1};
    mutable std::vector<interaction_line> cached_lines_;
};

class interaction_user_message : public agent_interaction {
public:
    explicit interaction_user_message(std::string text) : text_(std::move(text)) {}
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

class interaction_llm_response : public agent_interaction {
public:
    explicit interaction_llm_response(std::string text) : text_(std::move(text)) {}
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

class interaction_tool_call : public agent_interaction {
public:
    explicit interaction_tool_call(std::string text) : text_(std::move(text)) {}
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

class interaction_tool_result : public agent_interaction {
public:
    explicit interaction_tool_result(std::string text) : text_(std::move(text)) {}
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

class interaction_system_message : public agent_interaction {
public:
    explicit interaction_system_message(std::string text) : text_(std::move(text)) {}
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib