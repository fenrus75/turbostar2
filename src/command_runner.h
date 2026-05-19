#pragma once

#include <string>

class command_runner {
public:
    virtual ~command_runner() = default;

    // The core execution engine.
    // - Handles popen/pclose internally.
    // - Uses poll() to ensure we don't block indefinitely without checking `should_continue()`.
    // - Parses the output stream into discrete lines (handling \r\n and partial buffers).
    // - Blocks until the process finishes or `should_continue()` returns false.
    // Returns the exit code (or a distinct error code if interrupted).
    int execute(const std::string& command);

protected:
    // ------------------------------------------------------------------------
    // Required Virtuals
    // ------------------------------------------------------------------------
    
    // Called sequentially for every complete line of output (stdout + stderr combined).
    // Subclasses implement this to stream to a document, a logger, or accumulate to a string.
    virtual void on_output_line(const std::string& line) = 0;

    // ------------------------------------------------------------------------
    // Optional Virtuals
    // ------------------------------------------------------------------------

    // Allows subclasses to interrupt a running command. 
    // The execution loop will poll this periodically. Default is always true.
    virtual bool should_continue() const {
        return true; 
    }

    // Hook to rewrite the command before it is passed to popen.
    // This is where the future "sandboxing" strategy (e.g., systemd-run) will be injected.
    virtual std::string build_command(const std::string& raw_command) const {
        return raw_command; // Default: no sandboxing
    }
};

class sync_command_runner : public command_runner {
public:
    // Convenience wrapper that executes and returns the accumulated string directly
    std::string execute_and_get_output(const std::string& command);
    
    int get_exit_code() const { return exit_code_; }

protected:
    void on_output_line(const std::string& line) override;

private:
    std::string full_output_;
    int exit_code_ = 0;
};
