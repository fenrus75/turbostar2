#include "agentlib/ai_agent.h"
#include "agent_add_todo.h"
#include <sstream>
#include <cctype>

namespace tools
{

static std::string trim(const std::string &str)
{
	size_t first = str.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return "";
	size_t last = str.find_last_not_of(" \t\r\n");
	return str.substr(first, (last - first + 1));
}

static std::string strip_prefix(std::string s)
{
	s = trim(s);
	if (s.empty())
		return "";

	size_t i = 0;
	if (s[i] == '[' || s[i] == '(') {
		i++;
	}

	size_t start_chars = 0;
	while (i < s.length() && std::isalnum(static_cast<unsigned char>(s[i]))) {
		i++;
		start_chars++;
	}

	if (start_chars > 0) {
		if (i < s.length() && (s[i] == '.' || s[i] == ')' || s[i] == ']')) {
			i++;
			while (i < s.length() && (s[i] == ' ' || s[i] == '\t')) {
				i++;
			}
			return s.substr(i);
		}
	} else {
		if (s[i] == '-' || s[i] == '*') {
			i++;
			while (i < s.length() && (s[i] == ' ' || s[i] == '\t')) {
				i++;
			}
			return s.substr(i);
		}
		if (s.length() - i >= 3 &&
		    static_cast<unsigned char>(s[i]) == 0xe2 &&
		    static_cast<unsigned char>(s[i + 1]) == 0x80 &&
		    static_cast<unsigned char>(s[i + 2]) == 0xa2) {
			i += 3;
			while (i < s.length() && (s[i] == ' ' || s[i] == '\t')) {
				i++;
			}
			return s.substr(i);
		}
	}

	return s;
}

agent_add_todo_tool::agent_add_todo_tool(std::string text) : llm_tool_action("Adding todo: " + text), text_(std::move(text))
{
}

bool agent_add_todo_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (ctx.active_agent->is_read_only()) {
		out_error = "Execution Error: Agent is in read-only mode.";
		return false;
	}
	if (text_.empty()) {
		out_error = "Execution Error: Todo text cannot be empty.";
		return false;
	}
	return true;
}

std::string agent_add_todo_tool::execute(agentlib::tool_context &ctx)
{
	std::vector<std::string> lines;
	std::stringstream ss(text_);
	std::string line;
	while (std::getline(ss, line)) {
		std::string clean = strip_prefix(line);
		if (!clean.empty()) {
			lines.push_back(clean);
		}
	}

	if (lines.empty()) {
		return "No todo items added (text was empty or whitespace).";
	}

	for (const auto &l : lines) {
		ctx.active_agent->add_todo(l);
	}

	set_success(ctx);

	if (lines.size() == 1) {
		return "Added todo: " + lines[0];
	}
	return std::to_string(lines.size()) + " todo items added";
}

} // namespace tools
