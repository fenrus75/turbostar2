#include "address_lookup.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::cerr << std::format("Usage: {} <crash_file_path> <parent_pid>\n", argv[0]);
		return 1;
	}

	std::string crash_path = argv[1];
	std::string parent_pid = argv[2];
	std::string maps_path = std::format("/proc/{}/maps", parent_pid);

	if (!std::filesystem::exists(crash_path)) {
		std::cerr << std::format("Crash file does not exist: {}\n", crash_path);
		return 1;
	}

	std::ifstream infile(crash_path);
	if (!infile.is_open()) {
		std::cerr << std::format("Could not open crash file for reading: {}\n", crash_path);
		return 1;
	}

	std::vector<std::string> crash_lines;
	std::string line;
	while (std::getline(infile, line)) {
		crash_lines.push_back(line);
	}
	infile.close();

	struct frame_info {
		std::string label;
		std::string addr_str;
		uintptr_t addr{0};
	};

	std::vector<frame_info> frames;
	std::vector<uintptr_t> raw_addrs;

	for (const auto &cl : crash_lines) {
		size_t hash_pos = cl.find('#');
		size_t hex_pos = cl.find("0x");

		bool is_frame = (hash_pos != std::string::npos && hex_pos != std::string::npos && hex_pos > hash_pos);
		bool is_fault_addr = (cl.find("CrashAddress:") != std::string::npos && hex_pos != std::string::npos);

		if (!is_frame && !is_fault_addr) {
			continue;
		}

		std::string addr_str;
		for (size_t i = hex_pos; i < cl.length(); ++i) {
			char c = cl[i];
			if (std::isxdigit(c) || c == 'x') {
				addr_str += c;
			} else {
				break;
			}
		}

		try {
			uintptr_t addr = std::stoull(addr_str, nullptr, 16);
			std::string frame_label = is_frame ? cl.substr(0, hex_pos) : "  [Fault Address] ";
			frames.push_back({frame_label, addr_str, addr});
			raw_addrs.push_back(addr);
		} catch (...) {
			continue;
		}
	}

	auto resolved = turbostar::address_lookup::resolve_addresses(raw_addrs, maps_path);
	std::vector<std::string> resolved_stack_trace;

	for (size_t i = 0; i < frames.size() && i < resolved.size(); ++i) {
		const auto &f = frames[i];
		const auto &res = resolved[i];

		resolved_stack_trace.push_back(std::format("{}{} in {}", f.label, f.addr_str, res.function_name));
		resolved_stack_trace.push_back(std::format("    at {}", res.location));
	}

	if (!resolved_stack_trace.empty()) {
		std::ofstream outfile(crash_path, std::ios::app);
		if (outfile.is_open()) {
			outfile << "\n*** Resolved Stack Trace ***\n";
			for (const auto &rl : resolved_stack_trace) {
				outfile << rl << "\n";
			}
			outfile.close();
		}
	}

	return 0;
}
