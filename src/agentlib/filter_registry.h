#pragma once
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace agentlib
{

class filter_registry
{
      public:
	using filter_func = std::function<std::string(const std::string &)>;

	static filter_registry &get_instance();

	void register_filter(const std::string &name, filter_func func);
	void unregister_filter(const std::string &name);
	bool has_filter(const std::string &name) const;
	std::string apply_filter(const std::string &name, const std::string &input, bool &out_success) const;
	std::vector<std::string> get_registered_filters() const;

      private:
	filter_registry() = default;
	mutable std::mutex mutex_;
	std::map<std::string, filter_func> filters_;
};

} // namespace agentlib
