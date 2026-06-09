#include <cassert>
#include <iostream>
#include "../../src/event_logger.h"
#include "../../src/editor.h"

int main()
{
	std::cout << "Testing event_logger sequence numbers and slices..." << std::endl;
	event_logger &logger = event_logger::get_instance();

	logger.log("Before spike log 1");
	logger.log("Before spike log 2");

	uint64_t start_seq = logger.get_total_event_count();
	
	logger.log("Spike log 1: doing heavy work");
	logger.log("Spike log 2: still working");
	
	uint64_t end_seq = logger.get_total_event_count();

	logger.log("After spike log 1");

	assert(end_seq == start_seq + 2);

	std::vector<std::string> slice = logger.get_event_slice(start_seq, end_seq);
	assert(slice.size() == 2);
	assert(slice[0].find("Spike log 1: doing heavy work") != std::string::npos);
	assert(slice[1].find("Spike log 2: still working") != std::string::npos);

	std::cout << "Event logger slice tests passed!" << std::endl;

	std::cout << "Testing editor latency spike recording..." << std::endl;
	
	editor_options opts;
	opts.exit_immediately = 0.0;
	editor ed(opts);

	assert(ed.latency_spikes_.empty());

	// Now let's manually push a spike into the vector to test printout
	editor::latency_spike spike;
	spike.duration_ms = 22.5;
	spike.key_code = 120;
	spike.key_desc = "Char 'x'";
	spike.trace = {"[000100ms] Doing heavy task", "[000105ms] Done heavy task"};

	ed.latency_spikes_.push_back(spike);
	assert(ed.latency_spikes_.size() == 1);
	assert(ed.latency_spikes_[0].duration_ms == 22.5);
	assert(ed.latency_spikes_[0].key_desc == "Char 'x'");
	assert(ed.latency_spikes_[0].trace.size() == 2);

	std::cout << "--- Simulating printing latency report with spikes ---" << std::endl;
	ed.print_latency_report();

	std::cout << "All latency spike unit tests passed!" << std::endl;
	return 0;
}
