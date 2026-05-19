#include "process_runner.h"
#include <array>
#include <cstdio>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include "build_error_manager.h"
#include "command_runner.h"
#include "git_manager.h"

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
	build_error_manager::get_instance().clear();
	doc_->append_line("Running: " + command);

	is_running_.store(true);
	stop_requested_.store(false);

	worker_thread_ = std::thread(&process_runner::worker_loop, this, command);
}

void process_runner::stop()
{
	stop_requested_.store(true);
	if (worker_thread_.joinable()) {
		worker_thread_.join();
	}
	is_running_.store(false);
}

class streaming_command_runner : public command_runner {
public:
	streaming_command_runner(std::shared_ptr<document> doc, build_log_parser* parser, 
	                         std::atomic<bool>& stop_flag, std::atomic<bool>& auto_scroll)
		: doc_(doc), parser_(parser), stop_requested_(stop_flag), auto_scroll_(auto_scroll), line_count_(0) {}

protected:
	void on_output_line(const std::string& line) override {
		doc_->append_line(line);

		if (parser_) {
			std::vector<build_error> errs;
			parser_->parse_line(line, line_count_, errs);
			for (const auto &e : errs) {
				build_error_manager::get_instance().add_error(e);
			}
		}

		if (auto_scroll_.load()) {
			doc_->move_to_bottom();
		}
		
		line_count_++;
	}

	bool should_continue() const override {
		return !stop_requested_.load();
	}

private:
	std::shared_ptr<document> doc_;
	build_log_parser* parser_;
	std::atomic<bool>& stop_requested_;
	std::atomic<bool>& auto_scroll_;
	int line_count_;
};

void process_runner::worker_loop(std::string command)
{
	streaming_command_runner runner(doc_, parser_.get(), stop_requested_, auto_scroll_);
	runner.apply_build_profile();
	int exit_code = runner.execute(command + " 2>&1");
	
	doc_->append_line("");
	doc_->append_line("Process exited with code " + std::to_string(exit_code));
	if (auto_scroll_.load()) {
		doc_->move_to_bottom();
	}
	
	is_running_.store(false);

	// Dispatch a redraw event so existing windows show the new errors
	doc_->request_redraw();
}
