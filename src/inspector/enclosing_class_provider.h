#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include "codemap_utils.h"
#include "inspector/inspector_provider.h"

namespace turbostar
{

/**
 * @brief Fast, synchronous inspector provider that surfaces the enclosing class
 * for the function where the cursor currently resides (ambient context, 0ms latency).
 */
class enclosing_class_provider : public inspector_provider
{
      public:
	enclosing_class_provider() = default;
	~enclosing_class_provider() override = default;

	std::string_view name() const noexcept override { return "enclosing_class"; }

	inspector_result query(const inspector_query &q) override;

	void clear_cache();

      private:
	std::string make_cache_key(const inspector_query &q) const;
	std::optional<tools::class_summary_info> lookup_cache(const std::string &key);
	void store_cache(const std::string &key, const tools::class_summary_info &summary);

	/**
	 * @brief Mutex protecting the enclosing class lookup cache.
	 *
	 * Protects:
	 * - cache_ (unordered_map storing enclosing class summaries keyed by file and line)
	 *
	 * Locking guidelines:
	 * - Acquired briefly in lookup_cache(), store_cache(), and clear_cache().
	 * - Never hold this mutex across I/O or background operations.
	 */
	mutable std::mutex cache_mutex_;
	std::unordered_map<std::string, tools::class_summary_info> cache_;
};

} // namespace turbostar
