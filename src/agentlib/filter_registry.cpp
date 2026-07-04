#include "filter_registry.h"

namespace agentlib
{

filter_registry &filter_registry::get_instance()
{
	static filter_registry *instance = new filter_registry();
	return *instance;
}

void filter_registry::register_filter(const std::string &name, filter_func func)
{
	std::lock_guard<std::mutex> lock(mutex_);
	filters_[name] = std::move(func);
}

void filter_registry::unregister_filter(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	filters_.erase(name);
}

bool filter_registry::has_filter(const std::string &name) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return filters_.contains(name);
}

std::string filter_registry::apply_filter(const std::string &name, const std::string &input, bool &out_success) const
{
	filter_func func;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = filters_.find(name);
		if (it == filters_.end()) {
			out_success = false;
			return "";
		}
		func = it->second;
	}
	out_success = true;
	return func(input);
}

std::vector<std::string> filter_registry::get_registered_filters() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::string> names;
	for (const auto &[name, _] : filters_) {
		names.push_back(name);
	}
	return names;
}

} // namespace agentlib
