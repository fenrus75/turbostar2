#include "address_lookup.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>
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
	uintptr_t fault_addr = 0;
	bool has_fault_addr = false;
	uintptr_t rsp_addr = 0;
	bool has_rsp = false;

	for (const auto &cl : crash_lines) {
		size_t hash_pos = cl.find('#');
		size_t hex_pos = cl.find("0x");

		bool is_frame = (hash_pos != std::string::npos && hex_pos != std::string::npos && hex_pos > hash_pos);
		bool is_fault_addr = (cl.find("CrashAddress:") != std::string::npos && hex_pos != std::string::npos);
		bool is_rsp = (cl.find("RSP: 0x") != std::string::npos);

		if (is_rsp && !has_rsp) {
			size_t rsp_hex = cl.find("0x", cl.find("RSP:"));
			if (rsp_hex != std::string::npos) {
				std::string r_str;
				for (size_t i = rsp_hex; i < cl.length(); ++i) {
					char c = cl[i];
					if (std::isxdigit(c) || c == 'x') {
						r_str += c;
					} else {
						break;
					}
				}
				try {
					rsp_addr = std::stoull(r_str, nullptr, 16);
					has_rsp = true;
				} catch (...) {
				}
			}
		}

		if (is_fault_addr && !has_fault_addr) {
			std::string f_str;
			for (size_t i = hex_pos; i < cl.length(); ++i) {
				char c = cl[i];
				if (std::isxdigit(c) || c == 'x') {
					f_str += c;
				} else {
					break;
				}
			}
			try {
				fault_addr = std::stoull(f_str, nullptr, 16);
				has_fault_addr = true;
			} catch (...) {
			}
		}

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

	// Memory region classification
	struct vm_area {
		uintptr_t start{0};
		uintptr_t end{0};
		std::string perms;
		uintptr_t offset{0};
		std::string pathname;
	};

	std::vector<vm_area> areas;
	std::ifstream maps_file(maps_path);
	if (maps_file.is_open()) {
		std::string map_line;
		while (std::getline(maps_file, map_line)) {
			std::istringstream iss(map_line);
			std::string range, perms, offset_str, dev, inode_str;
			if (!(iss >> range >> perms >> offset_str >> dev >> inode_str)) {
				continue;
			}
			std::string pathname;
			std::getline(iss, pathname);
			size_t first_non_space = pathname.find_first_not_of(" \t");
			if (first_non_space != std::string::npos) {
				pathname = pathname.substr(first_non_space);
			} else {
				pathname.clear();
			}
			size_t dash = range.find('-');
			if (dash == std::string::npos) {
				continue;
			}
			try {
				vm_area va;
				va.start = std::stoull(range.substr(0, dash), nullptr, 16);
				va.end = std::stoull(range.substr(dash + 1), nullptr, 16);
				va.offset = std::stoull(offset_str, nullptr, 16);
				va.perms = perms;
				va.pathname = pathname;
				areas.push_back(va);
			} catch (...) {
			}
		}
	}

	std::vector<std::string> memory_region_analysis;
	if (has_fault_addr) {
		bool inside = false;
		for (const auto &va : areas) {
			if (fault_addr >= va.start && fault_addr < va.end) {
				inside = true;
				std::string name = va.pathname.empty() ? "[anonymous]" : va.pathname;
				memory_region_analysis.push_back(std::format("CrashAddress 0x{:x} is INSIDE mapped region: {} ({})", fault_addr, name, va.perms));
				memory_region_analysis.push_back(std::format("  Mapping range: [0x{:016x} - 0x{:016x}] (size: {} bytes, offset: +0x{:x})", va.start, va.end, va.end - va.start, fault_addr - va.start));
				if (has_rsp) {
					int64_t diff = static_cast<int64_t>(fault_addr) - static_cast<int64_t>(rsp_addr);
					memory_region_analysis.push_back(std::format("  Distance to RSP (0x{:016x}): {:+d} bytes", rsp_addr, diff));
				}
				break;
			}
		}
		if (!inside) {
			memory_region_analysis.push_back(std::format("CrashAddress 0x{:x} is UNMAPPED space.", fault_addr));
			if (fault_addr < 0x1000) {
				memory_region_analysis.push_back(std::format("  Likely cause: NULL pointer dereference or offset null member access (+0x{:x})", fault_addr));
			} else if (has_rsp) {
				int64_t diff = static_cast<int64_t>(fault_addr) - static_cast<int64_t>(rsp_addr);
				memory_region_analysis.push_back(std::format("  Distance to RSP (0x{:016x}): {:+d} bytes", rsp_addr, diff));
				if (std::abs(diff) <= 65536) {
					memory_region_analysis.push_back("  Likely cause: Near-stack access (possible stack overflow or dangling stack reference)");
				}
			}
			const vm_area *prev_area = nullptr;
			const vm_area *next_area = nullptr;
			for (const auto &va : areas) {
				if (va.end <= fault_addr) {
					if (!prev_area || va.end > prev_area->end) {
						prev_area = &va;
					}
				}
				if (va.start > fault_addr) {
					if (!next_area || va.start < next_area->start) {
						next_area = &va;
					}
				}
			}
			if (prev_area) {
				std::string name = prev_area->pathname.empty() ? "[anonymous]" : prev_area->pathname;
				memory_region_analysis.push_back(std::format("  Preceding mapping: [0x{:016x} - 0x{:016x}] {} (distance: -{} bytes)",
					prev_area->start, prev_area->end, name, fault_addr - prev_area->end));
			}
			if (next_area) {
				std::string name = next_area->pathname.empty() ? "[anonymous]" : next_area->pathname;
				memory_region_analysis.push_back(std::format("  Succeeding mapping: [0x{:016x} - 0x{:016x}] {} (distance: +{} bytes)",
					next_area->start, next_area->end, name, next_area->start - fault_addr));
			}
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

	if (!memory_region_analysis.empty() || !resolved_stack_trace.empty()) {
		std::ofstream outfile(crash_path, std::ios::app);
		if (outfile.is_open()) {
			if (!memory_region_analysis.empty()) {
				outfile << "\n*** Memory Region Analysis ***\n";
				for (const auto &ml : memory_region_analysis) {
					outfile << ml << "\n";
				}
			}
			if (!resolved_stack_trace.empty()) {
				outfile << "\n*** Resolved Stack Trace ***\n";
				for (const auto &rl : resolved_stack_trace) {
					outfile << rl << "\n";
				}
			}
			outfile.close();
		}
	}

	return 0;
}
