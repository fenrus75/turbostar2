#pragma once

#include "../command_runner.h"
#include "../agentlib/interactions/terminal.h"
#include <deque>
#include <string>
#include <functional>
#include <memory>

namespace tools {

class terminal_parser {
public:
    void process_chunk(const std::string& chunk) {
        full_output_ += chunk;
        for (char c : chunk) {
            if (c == '\n') {
                display_lines_.push_back(current_line_);
                if (display_lines_.size() > 15) {
                    display_lines_.pop_front();
                }
                current_line_.clear();
            } else if (c == '\r') {
                current_line_.clear(); // Overwrite current line (handle progress bars)
            } else {
                current_line_ += c;
            }
        }
    }

    std::string get_display_text() const {
        std::string res;
        for (const auto& line : display_lines_) {
            res += line + "\n";
        }
        res += current_line_;
        return res;
    }

    std::string get_full_output() const {
        return full_output_;
    }

private:
    std::deque<std::string> display_lines_;
    std::string current_line_;
    std::string full_output_;
};

class terminal_command_runner : public command_runner {
public:
    terminal_command_runner(std::shared_ptr<agentlib::interaction_terminal> interaction, 
                            std::function<void()> trigger_update)
        : interaction_(interaction), trigger_update_(std::move(trigger_update)) {}

    std::string get_final_output() const { return parser_.get_full_output(); }

protected:
    void on_output_chunk(const std::string& chunk) override {
        parser_.process_chunk(chunk);
        if (interaction_) {
            interaction_->set_text(parser_.get_display_text());
            if (trigger_update_) trigger_update_();
        }
    }
    void on_output_line(const std::string& /*line*/) override {}

private:
    terminal_parser parser_;
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
    std::function<void()> trigger_update_;
};

} // namespace tools