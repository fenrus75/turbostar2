#include "command_runner.h"
#include <array>
#include <cstdio>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>

int command_runner::execute(const std::string& command) {
    std::string final_command = build_command(command);
    FILE* pipe = popen(final_command.c_str(), "r");
    
    if (!pipe) {
        return -1; // Failed to start
    }

    int fd = fileno(pipe);
    std::array<char, 256> buffer;
    std::string current_line;

    while (should_continue()) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        
        int ret = poll(&pfd, 1, 100); // 100ms timeout
        if (ret < 0) {
            break; // poll error
        }
        if (ret == 0) {
            continue; // Timeout, check should_continue() again
        }

        if (fgets(buffer.data(), buffer.size(), pipe) == nullptr) {
            break; // EOF
        }
        
        current_line += buffer.data();
        
        size_t pos;
        while ((pos = current_line.find('\n')) != std::string::npos) {
            std::string line = current_line.substr(0, pos);
            
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            on_output_line(line);
            current_line = current_line.substr(pos + 1);
        }
    }

    // Flush any remaining characters without a newline
    if (!current_line.empty()) {
        if (!current_line.empty() && current_line.back() == '\r') {
            current_line.pop_back();
        }
        on_output_line(current_line);
    }

    int exit_code = pclose(pipe);
    return WEXITSTATUS(exit_code);
}

std::string sync_command_runner::execute_and_get_output(const std::string& command) {
    full_output_.clear();
    exit_code_ = execute(command);
    return full_output_;
}

void sync_command_runner::on_output_line(const std::string& line) {
    full_output_ += line + "\n";
}
