#include "filter_registry.h"
#include "markdown_utils.h"
#include "tools/output_filter.h"

namespace agentlib
{

filter_registry &filter_registry::get_instance()
{
	static filter_registry *instance = new filter_registry();
	return *instance;
}

static std::string strip_ansi(const std::string& input) {
	std::string output;
	output.reserve(input.length());
	size_t i = 0;
	while (i < input.length()) {
		if (input[i] == '\x1b') {
			if (i + 1 < input.length() && input[i + 1] == '[') {
				size_t j = i + 2;
				while (j < input.length()) {
					char c = input[j];
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
						i = j + 1;
						break;
					}
					j++;
				}
				if (j == input.length()) {
					output += ' ';
					i++;
				}
			} else {
				output += ' ';
				i++;
			}
		} else {
			output += input[i];
			i++;
		}
	}
	return output;
}

filter_registry::filter_registry()
{
	register_filter("strip_ansi", [](const std::string &input) {
		return strip_ansi(input);
	});

	register_filter("markdown_align_tables", [](const std::string &input) {
		return markdown_utils::align_all_tables(input, false);
	});

	register_filter("meson_compile", [](const std::string &input) {
		std::vector<std::shared_ptr<tools::output_filter>> filters = {
			std::make_shared<tools::meson_compile_filter>()
		};
		return tools::apply_output_filters("meson compile", input, filters);
	});

	register_filter("meson_test", [](const std::string &input) {
		std::vector<std::shared_ptr<tools::output_filter>> filters = {
			std::make_shared<tools::meson_test_filter>()
		};
		return tools::apply_output_filters("meson test", input, filters);
	});
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
