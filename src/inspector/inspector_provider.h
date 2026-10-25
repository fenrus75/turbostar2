#pragma once

#include <functional>
#include <string_view>
#include "inspector/inspector_types.h"

/*

# subclasses of inspector_provider

| subclass                    | filename                          |
| --------------------------- | --------------------------------- | 
| fallback_inspector_provider | src/inspector/fallback_provider.h |
| lsp_inspector_provider      | src/inspector/lsp_provider.h      |

*/

/**
 * @brief Abstract base class for companion window content providers.
 */
class inspector_provider
{
      public:
	virtual ~inspector_provider() = default;
	virtual std::string_view name() const noexcept = 0;

	// Synchronous query (< 1ms). Returns immediate best-effort data.
	virtual inspector_result query(const inspector_query &q) = 0;

	// Optional asynchronous resolution. Default is no-op.
	virtual void query_async(const inspector_query &q, std::function<void(inspector_result)> callback)
	{
		(void)q;
		(void)callback;
	}
};
