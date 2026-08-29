#include "agentlib/tool_tracer.h"
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace agentlib {

tool_tracer &tool_tracer::get_instance()
{
	static tool_tracer instance;
	return instance;
}

void tool_tracer::set_enabled(bool enabled) noexcept
{
	enabled_ = enabled;
}

bool tool_tracer::is_enabled() const noexcept
{
	return enabled_;
}

void tool_tracer::reset() noexcept
{
	enabled_ = false;
	counter_ = 0;
}

void tool_tracer::trace_tool_call(const std::string &tool_name, const std::string &args_json_string, const std::string &output_result)
{
	if (!enabled_) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	size_t current_id = counter_++;
	std::string filename = std::format("toolcall.{}", current_id);

	std::ofstream out(filename, std::ios::out | std::ios::trunc);
	if (!out.is_open()) {
		return;
	}

	nlohmann::json call_obj;
	call_obj["name"] = tool_name;
	try {
		if (!args_json_string.empty()) {
			call_obj["arguments"] = nlohmann::json::parse(args_json_string);
		} else {
			call_obj["arguments"] = nlohmann::json::object();
		}
	} catch (...) {
		call_obj["arguments"] = args_json_string;
	}

	out << call_obj.dump(2) << "\n---\n" << output_result;
}

} // namespace agentlib
