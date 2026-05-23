#include "run_python.h"
#include "../../command_runner.h"
#include "../../fs_utils.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <deque>
#include <sstream>

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

class live_python_runner : public command_runner {
public:
    live_python_runner(std::shared_ptr<agentlib::interaction_terminal> interaction, 
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

run_python_tool::run_python_tool(run_python_args args) : args_(std::move(args)) {
    std::string title = "Python Execution";
    if (args_.file_path) {
        title += " (" + *args_.file_path + ")";
    }
    interaction_ = std::make_shared<agentlib::interaction_terminal>(title, "Running...");
}

std::shared_ptr<agentlib::agent_interaction> run_python_tool::get_interaction() const {
    return interaction_;
}

bool run_python_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (args_.file_path) {
        std::string resolved_path;
        if (!ctx.fs_security.validate_access(*args_.file_path, agentlib::access_type::read, resolved_path, out_error)) {
            return false;
        }
        if (!std::filesystem::exists(resolved_path)) {
            out_error = "File does not exist: " + resolved_path;
            return false;
        }
    }
    return true;
}

std::string run_python_tool::execute(agentlib::tool_context& ctx) {
    live_python_runner runner(interaction_, ctx.trigger_ui_update);
    runner.apply_strict_agent_profile();
    runner.set_project_dir(ctx.fs_security.get_working_directory().string());
    
    // Allow uv cache explicitly if it exists
    const char* home = std::getenv("HOME");
    if (home) {
        std::string uv_cache = std::string(home) + "/.cache/uv";
        if (std::filesystem::exists(uv_cache)) {
            runner.add_extra_rw_path(uv_cache);
        }
    }

    std::string script_path;

    if (args_.code) {
        // We will use stdin
    } else {
        std::string resolved_path;
        std::string error;
        if (!ctx.fs_security.validate_access(*args_.file_path, agentlib::access_type::read, resolved_path, error)) {
            return "Error: " + error;
        }
        script_path = resolved_path;
    }

    std::string base_cmd;
    
    // Check if uv is available
    if (system("which uv > /dev/null 2>&1") == 0) {
        base_cmd = "PYTHONUNBUFFERED=1 uv run ";
        for (const auto& dep : args_.dependencies) {
            base_cmd += "--with '" + dep + "' ";
        }
    } else {
        base_cmd = "PYTHONUNBUFFERED=1 python3 -u ";
        if (!args_.dependencies.empty()) {
            return "Execution Error: Dependencies were requested but 'uv' is not installed on the host system.";
        }
    }

    std::string full_cmd;
    if (args_.code) {
        // Execute via stdin
        base_cmd += "- "; // Python / uv reads from stdin
        std::string escaped_code;
        for (char c : *args_.code) {
            if (c == '\'') escaped_code += "'\\''";
            else escaped_code += c;
        }
        full_cmd = "echo '" + escaped_code + "' | " + base_cmd;
    } else {
        full_cmd = base_cmd + "'" + script_path + "'";
    }

    runner.execute(full_cmd);
    std::string output = runner.get_final_output();
    
    std::string coredumps = runner.get_new_coredumps();
    if (!coredumps.empty()) {
        output += "\n\n" + coredumps;
    }

    if (output.empty()) {
        output = "Process finished successfully with no output.";
        if (interaction_) {
            interaction_->set_text(output);
            if (ctx.trigger_ui_update) {
                ctx.trigger_ui_update();
            }
        }
    }

    return output;
}

} // namespace tools