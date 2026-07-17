#include "fs_man_search.h"
#include "../../fs_utils.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>
#include <vector>
#include <set>

namespace tools {

fs_man_search_tool::fs_man_search_tool(fs_man_search_args args)
	: agentlib::llm_tool_action(std::format("Man page search: {}", args.query))
	, args_(std::move(args))
{}

bool fs_man_search_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
	// No runtime workspace resource validation is needed for global lookups.
	return true;
}

std::string fs_man_search_tool::execute(agentlib::tool_context& ctx) {
	const char* env_override = std::getenv("TURBOSTAR_MAN_DIR_OVERRIDE");
	std::string cmd;
	if (env_override && *env_override) {
		cmd = std::format("MANPATH={} man -k {}", fs_utils::escape_shell_arg(env_override), fs_utils::escape_shell_arg(args_.query));
	} else {
		cmd = std::format("man -k {}", fs_utils::escape_shell_arg(args_.query));
	}

	std::string output = fs_utils::execute_command_sync(cmd);

	std::vector<std::tuple<std::string, std::string, std::string>> results;
	std::set<std::pair<std::string, std::string>> seen;

	std::stringstream ss(output);
	std::string line;
	while (std::getline(ss, line)) {
		// Trim trailing whitespace
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
			line.pop_back();
		}
		// Trim leading whitespace
		size_t start = 0;
		while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
			start++;
		}
		if (start >= line.size()) {
			continue;
		}
		line = line.substr(start);

		if (line.starts_with("Process exited with code")) {
			continue;
		}
		if (line.starts_with("apropos:") || line.starts_with("man:")) {
			continue;
		}

		size_t open_paren = line.find('(');
		size_t close_paren = line.find(')');
		if (open_paren != std::string::npos && close_paren != std::string::npos && open_paren < close_paren) {
			std::string name = line.substr(0, open_paren);
			while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
				name.pop_back();
			}

			std::string sec = line.substr(open_paren + 1, close_paren - open_paren - 1);
			while (!sec.empty() && std::isspace(static_cast<unsigned char>(sec.back()))) {
				sec.pop_back();
			}
			size_t s_start = 0;
			while (s_start < sec.size() && std::isspace(static_cast<unsigned char>(sec[s_start]))) {
				s_start++;
			}
			sec = sec.substr(s_start);

			// Filter by section if specified
			if (args_.section.has_value() && !args_.section->empty()) {
				if (sec.find(*args_.section) == std::string::npos) {
					continue;
				}
			}

			std::string desc = line.substr(close_paren + 1);
			size_t dash = desc.find('-');
			if (dash != std::string::npos) {
				desc = desc.substr(dash + 1);
			}
			while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.back()))) {
				desc.pop_back();
			}
			size_t d_start = 0;
			while (d_start < desc.size() && std::isspace(static_cast<unsigned char>(desc[d_start]))) {
				d_start++;
			}
			desc = desc.substr(d_start);

			if (seen.insert({name, sec}).second) {
				results.push_back({name, sec, desc});
			}
		}
	}

	if (results.empty()) {
		std::string msg;
		if (args_.section.has_value()) {
			msg = std::format("No manual pages found matching '{}' in section '{}'.", args_.query, *args_.section);
		} else {
			msg = std::format("No manual pages found matching '{}'.", args_.query);
		}
		set_success(ctx, msg);
		return msg;
	}

	// Sort results alphabetically by name, then by section
	std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
		if (std::get<0>(a) != std::get<0>(b)) {
			return std::get<0>(a) < std::get<0>(b);
		}
		return std::get<1>(a) < std::get<1>(b);
	});

	std::string md = std::format("### Man Pages matching \"{}\"\n\n", args_.query);
	md += "| Page | Section | Description |\n";
	md += "| --- | --- | --- |\n";

	size_t limit = std::min(results.size(), size_t(50));
	for (size_t i = 0; i < limit; ++i) {
		md += std::format("| `{}` | {} | {} |\n", std::get<0>(results[i]), std::get<1>(results[i]), std::get<2>(results[i]));
	}

	if (results.size() > 50) {
		md += std::format("\n*Showed 50 of {} total matching pages. Refine your query or section to filter further.*\n", results.size());
	}

	set_success(ctx, std::format("Successfully found {} manual pages.", results.size()));
	return md;
}

} // namespace tools
