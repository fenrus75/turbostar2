#include "process_runner.h"
#include <array>
#include <cstdio>
#include <iostream>

process_runner::process_runner(std::shared_ptr<document> output_doc, int max_lines)
    : doc_(output_doc), max_lines_(max_lines)
{
}

process_runner::~process_runner()
{
	stop();
}

void process_runner::execute(const std::string &command)
{
	stop(); // Ensure any previous process is stopped

	doc_->clear();
	doc_->append_line("Running: " + command);

	is_running_.store(true);
	stop_requested_.store(false);

	worker_thread_ = std::thread(&process_runner::worker_loop, this, command);
}

void process_runner::stop()
{
	if (is_running_) {
		stop_requested_.store(true);
		// Note: pclose blocks until the process exits. If it's a long running process,
		// we might need a more aggressive kill via process groups, but for compiling
		// this simple popen wrapper is usually sufficient.
		if (worker_thread_.joinable()) {
			worker_thread_.join();
		}
	}
}

void process_runner::worker_loop(std::string command)
{
	// Redirect stderr to stdout so we capture both
	std::string full_command = command + " 2>&1";
	FILE *pipe = popen(full_command.c_str(), "r");
	
	if (!pipe) {
		doc_->append_line("Failed to start process.");
		is_running_.store(false);
		return;
	}

	std::array<char, 256> buffer;
	std::string current_line;

	while (!stop_requested_ && fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
		current_line += buffer.data();
		
		// If we find a newline, flush it to the document
		size_t pos;
		while ((pos = current_line.find('\n')) != std::string::npos) {
			std::string line = current_line.substr(0, pos);
			
			// Handle Windows CRLF just in case
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			
			doc_->append_line(line);
			doc_->trim_top_lines(max_lines_);
			
			// Always scroll to bottom to tail the output
			doc_->move_to_bottom();
			
			current_line = current_line.substr(pos + 1);
		}
	}

	// Flush any remaining characters without a newline
	if (!current_line.empty()) {
		doc_->append_line(current_line);
		doc_->trim_top_lines(max_lines_);
		doc_->move_to_bottom();
	}

	int exit_code = pclose(pipe);
	doc_->append_line("");
	doc_->append_line("Process exited with code " + std::to_string(WEXITSTATUS(exit_code)));
	doc_->move_to_bottom();
	
	is_running_.store(false);
}
