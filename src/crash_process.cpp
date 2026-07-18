#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

struct Mapping {
	uintptr_t start = 0;
	uintptr_t end = 0;
	uintptr_t offset = 0;
	std::string pathname;
};

// Runs a command safely without shell interpretation and returns stdout lines
static std::vector<std::string> run_command(const std::string &bin, const std::vector<std::string> &args)
{
	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return {};
	}

	pid_t pid = fork();
	if (pid == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return {};
	}

	if (pid == 0) {
		// Child: Redirect stdout to pipe
		dup2(pipefd[1], STDOUT_FILENO);
		// Redirect stderr to /dev/null to keep logs clean
		int dev_null = open("/dev/null", O_WRONLY);
		if (dev_null != -1) {
			dup2(dev_null, STDERR_FILENO);
			close(dev_null);
		}
		close(pipefd[0]);
		close(pipefd[1]);

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
	close(pipefd[1]);
	std::vector<std::string> lines;
	std::string current_line;
	char buf[256];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
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
	close(pipefd[0]);
	waitpid(pid, nullptr, 0);
	return lines;
}

// Check if a binary exists in PATH
static bool check_binary_exists(const std::string &name)
{
	return !run_command("which", {name}).empty();
}

// Parses parent process maps
static std::vector<Mapping> parse_maps(const std::string &maps_path)
{
	std::vector<Mapping> mappings;
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

		// Ensure it is executable
		if (perms.find('x') == std::string::npos) {
			continue;
		}

		std::string pathname;
		std::getline(iss, pathname);
		// Trim leading spaces/tabs from pathname
		size_t first_non_space = pathname.find_first_not_of(" \t");
		if (first_non_space != std::string::npos) {
			pathname = pathname.substr(first_non_space);
		} else {
			pathname.clear();
		}

		if (pathname.empty() || pathname[0] != '/') {
			continue; // Only care about mapped files with absolute paths
		}

		size_t dash = range.find('-');
		if (dash == std::string::npos) {
			continue;
		}

		try {
			Mapping m;
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

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::cerr << "Usage: " << argv[0] << " <crash_file_path> <parent_pid>" << std::endl;
		return 1;
	}

	std::string crash_path = argv[1];
	std::string parent_pid = argv[2];
	std::string maps_path = "/proc/" + parent_pid + "/maps";

	if (!std::filesystem::exists(crash_path)) {
		std::cerr << "Crash file does not exist: " << crash_path << std::endl;
		return 1;
	}

	std::vector<Mapping> mappings = parse_maps(maps_path);
	bool has_eu_addr2line = check_binary_exists("eu-addr2line");
	bool has_addr2line = check_binary_exists("addr2line");

	std::ifstream infile(crash_path);
	if (!infile.is_open()) {
		std::cerr << "Could not open crash file for reading: " << crash_path << std::endl;
		return 1;
	}

	std::vector<std::string> crash_lines;
	std::string line;
	while (std::getline(infile, line)) {
		crash_lines.push_back(line);
	}
	infile.close();

	std::vector<std::string> resolved_stack_trace;

	for (const auto &cl : crash_lines) {
		// Look for lines containing stack frames, e.g. "  #0 0x55bc7134bdff"
		// or instruction pointer info lines, e.g. "CrashAddress: 0x3e80009e530"
		size_t hash_pos = cl.find('#');
		size_t hex_pos = cl.find("0x");

		bool is_frame = (hash_pos != std::string::npos && hex_pos != std::string::npos && hex_pos > hash_pos);
		bool is_fault_addr = (cl.find("CrashAddress:") != std::string::npos && hex_pos != std::string::npos);

		if (!is_frame && !is_fault_addr) {
			continue;
		}

		// Extract hex address string
		std::string addr_str;
		for (size_t i = hex_pos; i < cl.length(); ++i) {
			char c = cl[i];
			if (std::isxdigit(c) || c == 'x') {
				addr_str += c;
			} else {
				break;
			}
		}

		uintptr_t addr = 0;
		try {
			addr = std::stoull(addr_str, nullptr, 16);
		} catch (...) {
			continue;
		}

		std::vector<std::string> output;

		if (has_eu_addr2line) {
			output = run_command("eu-addr2line", {"-M", maps_path, "-f", "-C", addr_str});
		} else if (has_addr2line) {
			// Fallback to manual mapping and addr2line
			for (const auto &m : mappings) {
				if (addr >= m.start && addr < m.end) {
					uintptr_t offset = addr - m.start + m.offset;
					std::stringstream ss;
					ss << "0x" << std::hex << offset;
					output = run_command("addr2line", {"-f", "-C", "-e", m.pathname, ss.str()});
					break;
				}
			}
		}

		if (!output.empty()) {
			std::string frame_label = is_frame ? cl.substr(0, hex_pos) : "  [Fault Address] ";
			if (output.size() >= 2) {
				resolved_stack_trace.push_back(frame_label + addr_str + " in " + output[0]);
				resolved_stack_trace.push_back("    at " + output[1]);
				// Log any inlines if returned
				for (size_t i = 2; i < output.size(); ++i) {
					resolved_stack_trace.push_back("    " + output[i]);
				}
			} else {
				resolved_stack_trace.push_back(frame_label + addr_str + " in " + output[0]);
			}
		}
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
