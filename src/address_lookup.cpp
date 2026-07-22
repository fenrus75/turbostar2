#include "address_lookup.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

namespace fs = std::filesystem;

namespace turbostar
{

std::vector<std::string> address_lookup::run_command(const std::string &bin, const std::vector<std::string> &args,
						      const std::string &stdin_input)
{
	int pipe_out[2];
	int pipe_in[2];

	if (pipe(pipe_out) == -1) {
		return {};
	}

	if (!stdin_input.empty()) {
		if (pipe(pipe_in) == -1) {
			close(pipe_out[0]);
			close(pipe_out[1]);
			return {};
		}
	}

	pid_t pid = fork();
	if (pid == -1) {
		close(pipe_out[0]);
		close(pipe_out[1]);
		if (!stdin_input.empty()) {
			close(pipe_in[0]);
			close(pipe_in[1]);
		}
		return {};
	}

	if (pid == 0) {
		// Child: Redirect stdout to pipe
		dup2(pipe_out[1], STDOUT_FILENO);
		close(pipe_out[0]);
		close(pipe_out[1]);

		if (!stdin_input.empty()) {
			dup2(pipe_in[0], STDIN_FILENO);
			close(pipe_in[0]);
			close(pipe_in[1]);
		}

		// Redirect stderr to /dev/null to keep logs clean
		int dev_null = open("/dev/null", O_WRONLY);
		if (dev_null != -1) {
			dup2(dev_null, STDERR_FILENO);
			close(dev_null);
		}

		std::vector<char *> argv;
		argv.push_back(const_cast<char *>(bin.c_str()));
		for (const auto &arg : args) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);

		execvp(bin.c_str(), argv.data());
		_exit(1);
	}

	// Parent
	close(pipe_out[1]);

	if (!stdin_input.empty()) {
		close(pipe_in[0]);
		size_t written = 0;
		while (written < stdin_input.size()) {
			ssize_t n = write(pipe_in[1], stdin_input.c_str() + written, stdin_input.size() - written);
			if (n <= 0)
				break;
			written += static_cast<size_t>(n);
		}
		close(pipe_in[1]);
	}

	std::vector<std::string> lines;
	std::string current_line;
	char buf[512];
	ssize_t n;
	while ((n = read(pipe_out[0], buf, sizeof(buf) - 1)) > 0) {
		for (ssize_t i = 0; i < n; ++i) {
			if (buf[i] == '\n') {
				lines.push_back(current_line);
				current_line.clear();
			} else {
				current_line += buf[i];
			}
		}
	}
	if (!current_line.empty()) {
		lines.push_back(current_line);
	}
	close(pipe_out[0]);
	waitpid(pid, nullptr, 0);
	return lines;
}

bool address_lookup::check_binary_exists(const std::string &name)
{
	if (name.starts_with('/')) {
		return fs::exists(name);
	}
	return fs::exists(std::format("/usr/bin/{}", name)) || fs::exists(std::format("/bin/{}", name));
}

std::vector<memory_mapping> address_lookup::parse_maps(const std::string &maps_path_or_pid)
{
	std::string maps_path = maps_path_or_pid;
	if (maps_path.empty()) {
		maps_path = "/proc/self/maps";
	} else if (std::all_of(maps_path.begin(), maps_path.end(), ::isdigit)) {
		maps_path = std::format("/proc/{}/maps", maps_path_or_pid);
	}

	std::vector<memory_mapping> mappings;
	std::ifstream infile(maps_path);
	if (!infile.is_open()) {
		return mappings;
	}

	std::string line;
	while (std::getline(infile, line)) {
		std::istringstream iss(line);
		std::string range, perms, offset_str, dev, inode_str;
		if (!(iss >> range >> perms >> offset_str >> dev >> inode_str)) {
			continue;
		}

		// Require executable permissions ('x')
		if (perms.find('x') == std::string::npos) {
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

		if (pathname.empty() || pathname[0] != '/') {
			continue;
		}

		size_t dash = range.find('-');
		if (dash == std::string::npos) {
			continue;
		}

		try {
			memory_mapping m;
			m.start = std::stoull(range.substr(0, dash), nullptr, 16);
			m.end = std::stoull(range.substr(dash + 1), nullptr, 16);
			m.offset = std::stoull(offset_str, nullptr, 16);
			m.pathname = pathname;
			mappings.push_back(m);
		} catch (...) {
			// Ignore parse errors on corrupted lines
		}
	}
	return mappings;
}

static std::unordered_map<uintptr_t, resolved_address> parse_addr2line_output(const std::vector<std::string> &lines)
{
	std::unordered_map<uintptr_t, resolved_address> resolved_map;

	uintptr_t current_addr = 0;
	bool has_addr = false;
	int state = 0; // 0 = expecting address, 1 = expecting func_name, 2 = expecting location

	resolved_address current_res;

	for (const std::string &raw_line : lines) {
		std::string line = raw_line;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}

		if (line.starts_with("0x") || line.starts_with("0X")) {
			if (has_addr) {
				resolved_map[current_addr] = current_res;
			}
			try {
				current_addr = std::stoull(line, nullptr, 16);
				has_addr = true;
			} catch (...) {
				has_addr = false;
			}
			current_res = resolved_address{.address = current_addr};
			state = 1;
			continue;
		}

		if (!has_addr) {
			continue;
		}

		if (state == 1) {
			current_res.function_name = (line == "??" || line.empty()) ? "" : line;
			state = 2;
		} else if (state == 2) {
			current_res.location = line;

			size_t colon_pos = line.find_last_of(':');
			if (colon_pos != std::string::npos) {
				current_res.file_path = line.substr(0, colon_pos);
				std::string line_part = line.substr(colon_pos + 1);
				size_t space_pos = line_part.find(' ');
				if (space_pos != std::string::npos) {
					line_part = line_part.substr(0, space_pos);
				}
				try {
					current_res.line_number = std::stoi(line_part);
				} catch (...) {
					current_res.line_number = 0;
				}
			} else {
				current_res.file_path = line;
				current_res.line_number = 0;
			}
			state = 3; // Finished primary frame for this address; skip any extra inlined outer frames
		}
	}

	if (has_addr) {
		resolved_map[current_addr] = current_res;
	}

	return resolved_map;
}

resolved_address address_lookup::resolve_address(uintptr_t address, const std::string &maps_path_or_pid)
{
	auto results = resolve_addresses({address}, maps_path_or_pid);
	if (!results.empty()) {
		return results[0];
	}
	return resolved_address{.address = address};
}

std::vector<resolved_address> address_lookup::resolve_addresses(const std::vector<uintptr_t> &addresses,
								const std::string &maps_path_or_pid)
{
	if (addresses.empty()) {
		return {};
	}

	// 1. Deduplicate unique addresses to avoid duplicate resolution queries
	std::vector<uintptr_t> unique_addrs = addresses;
	std::sort(unique_addrs.begin(), unique_addrs.end());
	unique_addrs.erase(std::unique(unique_addrs.begin(), unique_addrs.end()), unique_addrs.end());

	std::unordered_map<uintptr_t, resolved_address> resolved_map;

	// Determine maps path if applicable
	std::string maps_path = maps_path_or_pid;
	if (maps_path.empty()) {
		maps_path = "/proc/self/maps";
	} else if (std::all_of(maps_path.begin(), maps_path.end(), ::isdigit)) {
		maps_path = std::format("/proc/{}/maps", maps_path_or_pid);
	}

	std::string eu_addr2line_bin;
	if (fs::exists("/usr/bin/eu-addr2line")) {
		eu_addr2line_bin = "/usr/bin/eu-addr2line";
	} else if (fs::exists("/bin/eu-addr2line")) {
		eu_addr2line_bin = "/bin/eu-addr2line";
	}

	std::string addr2line_bin;
	if (fs::exists("/usr/bin/addr2line")) {
		addr2line_bin = "/usr/bin/addr2line";
	} else if (fs::exists("/bin/addr2line")) {
		addr2line_bin = "/bin/addr2line";
	}

	// 2. High-performance batch resolution using eu-addr2line (-M <maps_file>)
	if (!eu_addr2line_bin.empty() && fs::exists(maps_path)) {
		std::vector<std::string> args = {"-a", "-M", maps_path, "-f", "-C"};
		for (auto addr : unique_addrs) {
			args.push_back(std::format("0x{:x}", addr));
		}

		auto lines = run_command(eu_addr2line_bin, args);
		resolved_map = parse_addr2line_output(lines);
	} else if (!addr2line_bin.empty()) {
		// Fallback: Parse memory mappings and group relative offsets per ELF binary
		std::vector<memory_mapping> mappings = parse_maps(maps_path);

		// If maps_path_or_pid is a direct ELF binary path (not a maps file)
		if (mappings.empty() && fs::exists(maps_path_or_pid) && !fs::is_directory(maps_path_or_pid)) {
			std::vector<std::string> args = {"-a", "-f", "-C", "-e", maps_path_or_pid};
			for (auto addr : unique_addrs) {
				args.push_back(std::format("0x{:x}", addr));
			}

			auto lines = run_command(addr2line_bin, args);
			resolved_map = parse_addr2line_output(lines);
		} else {
			// Group addresses by mapped ELF pathname
			struct addr_offset {
				uintptr_t original_addr;
				uintptr_t rel_offset;
			};
			std::unordered_map<std::string, std::vector<addr_offset>> elf_groups;

			for (auto addr : unique_addrs) {
				auto it = std::lower_bound(mappings.begin(), mappings.end(), addr,
							   [](const memory_mapping &m, uintptr_t val) { return m.end <= val; });
				if (it != mappings.end() && addr >= it->start && addr < it->end) {
					uintptr_t rel_addr = addr - it->start + it->offset;
					elf_groups[it->pathname].push_back({addr, rel_addr});
				} else {
					resolved_map[addr] = resolved_address{.address = addr, .location = std::format("0x{:x}", addr)};
				}
			}

			for (const auto &pair : elf_groups) {
				const std::string &elf_path = pair.first;
				const auto &offsets = pair.second;

				std::vector<std::string> args = {"-a", "-f", "-C", "-e", elf_path};
				for (const auto &item : offsets) {
					args.push_back(std::format("0x{:x}", item.rel_offset));
				}

				auto lines = run_command(addr2line_bin, args);
				auto sub_map = parse_addr2line_output(lines);

				for (const auto &item : offsets) {
					auto it = sub_map.find(item.rel_offset);
					if (it != sub_map.end()) {
						resolved_address res = it->second;
						res.address = item.original_addr;
						resolved_map[item.original_addr] = res;
					} else {
						resolved_map[item.original_addr] = resolved_address{.address = item.original_addr};
					}
				}
			}
		}
	}

	// Reconstruct output vector preserving original input ordering
	std::vector<resolved_address> result;
	result.reserve(addresses.size());
	for (auto addr : addresses) {
		auto it = resolved_map.find(addr);
		if (it != resolved_map.end()) {
			result.push_back(it->second);
		} else {
			result.push_back(resolved_address{.address = addr, .location = std::format("0x{:x}", addr)});
		}
	}

	return result;
}

} // namespace turbostar
