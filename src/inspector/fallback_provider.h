#pragma once

#include "inspector/inspector_provider.h"

/**
 * @brief Basic fallback provider displaying the current token or an idle notice.
 */
class fallback_inspector_provider : public inspector_provider
{
      public:
	fallback_inspector_provider() = default;
	~fallback_inspector_provider() override = default;

	std::string_view name() const noexcept override { return "token"; }
	inspector_result query(const inspector_query &q) override;
};
