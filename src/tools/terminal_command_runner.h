#pragma once

#include "../command_runner.h"
#include "../agentlib/interactions/terminal.h"
#include "../utf8.h"
#include <deque>
#include <string>
#include <functional>
#include <memory>

namespace tools {

class terminal_parser {
public:
    void process_chunk(const std::string& chunk) {
        full_output_ += chunk;
        
        // Handle ANSI Clear Screen: \033[2J (Clear entire screen) or \033c (Reset device)
        size_t clear_pos = full_output_.rfind("\033[2J");
        size_t reset_pos = full_output_.rfind("\033c");
        size_t wipe_pos = std::string::npos;
        
        if (clear_pos != std::string::npos) wipe_pos = clear_pos + 4;
        if (reset_pos != std::string::npos && (wipe_pos == std::string::npos || reset_pos + 2 > wipe_pos)) {
            wipe_pos = reset_pos + 2;
        }

        if (wipe_pos != std::string::npos) {
            // A clear screen signal was detected. Wipe everything before it.
            full_output_ = full_output_.substr(wipe_pos);
            display_lines_.clear();
            current_line_.clear();
            
            // Re-process the remaining output to rebuild the UI display lines
            for (char c : full_output_) {
                if (c == '\n') {
                    display_lines_.push_back(current_line_);
                    if (display_lines_.size() > 15) display_lines_.pop_front();
                    current_line_.clear();
                } else if (c == '\r') {
                    current_line_.clear();
                } else {
                    current_line_ += c;
                }
            }
            return;
        }

        // If no clear screen was found, just process the new chunk normally
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
        : interaction_(interaction), trigger_update_(std::move(trigger_update)) {
        start_time_ = std::chrono::steady_clock::now();
    }

    std::string get_final_output() const { return utf8::sanitize(parser_.get_full_output()); }
    
    void set_timeout(int seconds) { timeout_seconds_ = seconds; }

protected:
    bool should_continue() const override {
        if (!command_runner::should_continue()) {
            return false;
        }
        if (timeout_seconds_ > 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count() > timeout_seconds_) {
                return false;
            }
        }
        return true;
    }

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
    std::chrono::time_point<std::chrono::steady_clock> start_time_;
    int timeout_seconds_{0};
};

} // namespace tools