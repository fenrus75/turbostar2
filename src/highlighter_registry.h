#pragma once

#include <vector>
#include <memory>
#include <string>
#include "syntax_highlighter.h"

/**
 * @brief Singleton registry holding all available syntax highlighters.
 */
class highlighter_registry
{
      public:
	static highlighter_registry &get_instance();

	/**
	 * @brief Returns the first highlighter that supports the filename, or a default one.
	 */
	std::shared_ptr<syntax_highlighter> get_highlighter_for_file(const std::string &filename) const;

      private:
	highlighter_registry();
	~highlighter_registry() = default;

	std::vector<std::shared_ptr<syntax_highlighter>> highlighters_;
};
