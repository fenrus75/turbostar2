#include "highlighter/highlighter_registry.h"
#include "highlighter/cpp_highlighter.h"
#include "highlighter/markdown_highlighter.h"
#include "highlighter/python_highlighter.h"
#include "highlighter/verilog_highlighter.h"
#include "highlighter/html_highlighter.h"

// A default highlighter that does nothing
class default_highlighter : public syntax_highlighter
{
      public:
	bool supports_file(const std::string &) const override
	{
		return true;
	} // Fallback
	void highlight(std::shared_ptr<line> l) override
	{
		std::vector<syntax_attribute> attrs(l->length_in_chars(), syntax_attribute::normal);
		l->set_attributes(attrs);
	}
};

highlighter_registry &highlighter_registry::get_instance()
{
	static highlighter_registry instance;
	return instance;
}

highlighter_registry::highlighter_registry()
{
	// Order matters: first match wins
	highlighters_.push_back(std::make_shared<cpp_highlighter>());
	highlighters_.push_back(std::make_shared<markdown_highlighter>());
	highlighters_.push_back(std::make_shared<python_highlighter>());
	highlighters_.push_back(std::make_shared<verilog_highlighter>());
	highlighters_.push_back(std::make_shared<html_highlighter>());
}

std::shared_ptr<syntax_highlighter> highlighter_registry::get_highlighter_for_file(const std::string &filename) const
{
	for (const auto &hl : highlighters_) {
		if (hl->supports_file(filename)) {
			return hl;
		}
	}
	// Fallback
	return std::make_shared<default_highlighter>();
}

std::shared_ptr<syntax_highlighter> highlighter_registry::get_highlighter_for_language(const std::string &lang) const
{
	for (const auto &hl : highlighters_) {
		if (hl->supports_language(lang)) {
			return hl;
		}
	}
	return nullptr;
}
