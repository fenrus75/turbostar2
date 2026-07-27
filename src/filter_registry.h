#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace agentlib
{

class filter_registry
{
      public:
	using filter_func = std::function<std::string(const std::string &)>;

	static filter_registry &get_instance();

	void register_filter(std::string_view name, filter_func func, const std::vector<std::string> &categories = {});
	void unregister_filter(std::string_view name);
	bool has_filter(std::string_view name) const;
	std::string apply_filter(std::string_view name, std::string_view input, bool &out_success) const;
	std::vector<std::string> get_registered_filters(std::string_view category = "") const;

      private:
	filter_registry();
	mutable std::mutex mutex_;
	
	struct filter_info {
		filter_func func;
		std::vector<std::string> categories;
	};
	std::map<std::string, filter_info> filters_;
};

} // namespace agentlib
