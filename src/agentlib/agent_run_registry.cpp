#include "agent_run_registry.h"
#include <algorithm>
#include <cctype>
#include <format>
#include "utf8.h"

namespace agentlib
{

void agent_run_registry::register_app_run(int app_run_id, std::string_view binary, int linked_gdb_id)
{
	if (app_run_id < 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	run_session_info info;
	info.run_id = app_run_id;
	info.kind = run_target_kind::app;
	info.linked_run_id = linked_gdb_id;
	info.binary_path = std::string(binary);
	info.is_alive = true;
	info.was_terminated = false;
	info.created_at = std::chrono::steady_clock::now();

	runs_[app_run_id] = std::move(info);
	history_.push_back(app_run_id);
}

void agent_run_registry::register_gdb_run(int gdb_run_id, int linked_app_id)
{
	if (gdb_run_id < 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	run_session_info info;
	info.run_id = gdb_run_id;
	info.kind = run_target_kind::debugger;
	info.linked_run_id = linked_app_id;
	info.binary_path = "";
	info.is_alive = true;
	info.was_terminated = false;
	info.created_at = std::chrono::steady_clock::now();

	runs_[gdb_run_id] = std::move(info);
	history_.push_back(gdb_run_id);
}

std::vector<int> agent_run_registry::mark_terminated(int run_id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<int> terminated;
	if (run_id < 0) {
		return terminated;
	}

	auto it = runs_.find(run_id);
	if (it != runs_.end()) {
		it->second.is_alive = false;
		it->second.was_terminated = true;
		terminated.push_back(run_id);

		int linked = it->second.linked_run_id;
		if (linked >= 0) {
			auto linked_it = runs_.find(linked);
			if (linked_it != runs_.end()) {
				linked_it->second.is_alive = false;
				linked_it->second.was_terminated = true;
				terminated.push_back(linked);
			}
		}
	} else {
		// Untracked run explicitly closed; record it as terminated
		run_session_info info;
		info.run_id = run_id;
		info.kind = run_target_kind::app;
		info.linked_run_id = -1;
		info.is_alive = false;
		info.was_terminated = true;
		info.created_at = std::chrono::steady_clock::now();
		runs_[run_id] = std::move(info);
		terminated.push_back(run_id);
	}

	return terminated;
}

bool agent_run_registry::is_already_terminated(int run_id) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = runs_.find(run_id);
	if (it != runs_.end()) {
		return it->second.was_terminated;
	}
	return false;
}

int agent_run_registry::get_linked_run_id(int run_id) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = runs_.find(run_id);
	if (it != runs_.end()) {
		return it->second.linked_run_id;
	}
	return -1;
}

std::optional<int> agent_run_registry::get_latest_gdb_id(bool only_alive) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
		auto r_it = runs_.find(*it);
		if (r_it != runs_.end() && r_it->second.kind == run_target_kind::debugger) {
			if (!only_alive || r_it->second.is_alive) {
				return *it;
			}
		}
	}
	return std::nullopt;
}

std::optional<int> agent_run_registry::get_latest_app_id(bool only_alive) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
		auto r_it = runs_.find(*it);
		if (r_it != runs_.end() && r_it->second.kind == run_target_kind::app) {
			if (!only_alive || r_it->second.is_alive) {
				return *it;
			}
		}
	}
	return std::nullopt;
}

std::optional<int> agent_run_registry::get_latest_run_id(bool only_alive) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
		auto r_it = runs_.find(*it);
		if (r_it != runs_.end()) {
			if (!only_alive || r_it->second.is_alive) {
				return *it;
			}
		}
	}
	return std::nullopt;
}

std::optional<int> agent_run_registry::resolve_run_id(const std::optional<std::string> &untrusted_id, run_target_kind preferred_kind) const
{
	std::lock_guard<std::mutex> lock(mutex_);

	auto find_latest = [&](run_target_kind kind, bool only_alive) -> std::optional<int> {
		for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
			auto r_it = runs_.find(*it);
			if (r_it != runs_.end() && r_it->second.kind == kind) {
				if (!only_alive || r_it->second.is_alive) {
					return *it;
				}
			}
		}
		return std::nullopt;
	};

	auto find_any_latest = [&](bool only_alive) -> std::optional<int> {
		for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
			auto r_it = runs_.find(*it);
			if (r_it != runs_.end()) {
				if (!only_alive || r_it->second.is_alive) {
					return *it;
				}
			}
		}
		return std::nullopt;
	};

	// 1. Omitted / empty string -> intelligent default based on preferred_kind
	if (!untrusted_id.has_value() || untrusted_id->empty()) {
		if (preferred_kind == run_target_kind::debugger) {
			if (auto id = find_latest(run_target_kind::debugger, true))
				return id;
			if (auto id = find_any_latest(true))
				return id;
			if (auto id = find_latest(run_target_kind::debugger, false))
				return id;
			return find_any_latest(false);
		} else {
			if (auto id = find_latest(run_target_kind::app, true))
				return id;
			if (auto id = find_any_latest(true))
				return id;
			if (auto id = find_latest(run_target_kind::app, false))
				return id;
			return find_any_latest(false);
		}
	}

	std::string clean = utf8::to_lower(*untrusted_id);

	// 2. Symbolic aliases
	if (clean == "gdb" || clean == "debugger") {
		if (auto id = find_latest(run_target_kind::debugger, true))
			return id;
		return find_latest(run_target_kind::debugger, false);
	}
	if (clean == "app" || clean == "main") {
		if (auto id = find_latest(run_target_kind::app, true))
			return id;
		return find_latest(run_target_kind::app, false);
	}
	if (clean == "latest" || clean == "last") {
		if (auto id = find_any_latest(true))
			return id;
		return find_any_latest(false);
	}

	// 3. Optional "run_" prefix (e.g. "run_1010")
	if (clean.starts_with("run_")) {
		clean = clean.substr(4);
	}

	// 4. Numeric parsing
	try {
		size_t pos = 0;
		int parsed = std::stoi(clean, &pos);
		if (pos == clean.size() && parsed >= 0) {
			return parsed;
		}
	} catch (...) {
	}

	return std::nullopt;
}

bool agent_run_registry::resolve_from_json(const nlohmann::json &untrusted_args, run_target_kind preferred_kind, int &out_run_id,
					   std::string &out_error) const
{
	std::optional<std::string> raw_param;
	if (untrusted_args.contains("run_id") && !untrusted_args["run_id"].is_null()) {
		const auto &val = untrusted_args["run_id"];
		if (val.is_number_integer()) {
			out_run_id = val.get<int>();
			if (out_run_id < 0) {
				out_error = "Invalid run_id specified: ID must be non-negative.";
				return false;
			}
			return true;
		} else if (val.is_string()) {
			raw_param = val.get<std::string>();
		} else {
			out_error = "Invalid run_id specified: Expected integer or alias string ('gdb', 'app', 'latest').";
			return false;
		}
	}

	auto res = resolve_run_id(raw_param, preferred_kind);
	if (!res.has_value()) {
		if (raw_param.has_value()) {
			out_error =
			    std::format("Invalid run_id '{}': unknown alias or format. Valid aliases: 'gdb', 'app', 'latest'.", *raw_param);
		} else {
			out_error = "Missing required field 'run_id': No active run found to target. Specify an explicit run_id or launch "
				    "an executable first.";
		}
		return false;
	}

	out_run_id = *res;
	return true;
}

std::optional<run_session_info> agent_run_registry::get_run_info(int run_id) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = runs_.find(run_id);
	if (it != runs_.end()) {
		return it->second;
	}
	return std::nullopt;
}

size_t agent_run_registry::size() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return runs_.size();
}

void agent_run_registry::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	runs_.clear();
	history_.clear();
}

} // namespace agentlib
