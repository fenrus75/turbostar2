#include "filter_registry.h"
#include "markdown_utils.h"
#include "tools/output_filter.h"
#include "utf8.h"

namespace agentlib
{

filter_registry &filter_registry::get_instance()
{
	static filter_registry *instance = new filter_registry();
	return *instance;
}

static std::string strip_utf8(const std::string& input) {
	std::string output;
	output.reserve(input.length());
	for (char c : input) {
		unsigned char uc = static_cast<unsigned char>(c);
		if (uc <= 127) {
			output += c;
		}
	}
	return output;
}

filter_registry::filter_registry()
{
	register_filter("strip_utf8", [](const std::string &input) {
		return strip_utf8(input);
	}, {"text"});

	register_filter("strip_ansi", [](const std::string &input) {
		return utf8::sanitize_terminal_output(input);
	}, {"text"});

	register_filter("markdown_align_tables", [](const std::string &input) {
		return markdown_utils::align_all_tables(input, false);
	}, {"text"});

	register_filter("meson_compile", [](const std::string &input) {
		std::vector<std::shared_ptr<tools::output_filter>> filters = {
			std::make_shared<tools::meson_compile_filter>()
		};
		return tools::apply_output_filters("meson compile", input, filters);
	}, {"build"});

	register_filter("meson_test", [](const std::string &input) {
		std::vector<std::shared_ptr<tools::output_filter>> filters = {
			std::make_shared<tools::meson_test_filter>()
		};
		return tools::apply_output_filters("meson test", input, filters);
	}, {"build"});
}

void filter_registry::register_filter(const std::string &name, filter_func func, const std::vector<std::string> &categories)
{
	std::lock_guard<std::mutex> lock(mutex_);
	filters_[name] = filter_info{std::move(func), categories};
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
		func = it->second.func;
	}
	out_success = true;
	return func(input);
}

std::vector<std::string> filter_registry::get_registered_filters(const std::string &category) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::string> names;
	for (const auto &[name, info] : filters_) {
		if (category.empty()) {
			names.push_back(name);
		} else {
			for (const auto &cat : info.categories) {
				if (cat == category) {
					names.push_back(name);
					break;
				}
			}
		}
	}
	return names;
}

} // namespace agentlib
