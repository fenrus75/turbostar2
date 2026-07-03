#pragma once

#include "hex/hex_highlighter.h"
#include <memory>
#include <vector>

class hex_highlighter_registry
{
      public:
	static hex_highlighter_registry &get_instance();

	// Selects the appropriate highlighter (returns nullptr if none match)
	std::shared_ptr<hex_highlighter> detect_highlighter(const std::vector<uint8_t> &data) const;

      private:
	hex_highlighter_registry();
	~hex_highlighter_registry() = default;

	std::vector<std::shared_ptr<hex_highlighter>> highlighters_;
};
