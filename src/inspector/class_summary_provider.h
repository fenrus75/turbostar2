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

struct cached_class_summary {
	bool exists{false};
	tools::class_summary_info summary;
};

/**
 * @brief Inspector provider that surfaces class structure summaries (key fields and methods)
 * for the class enclosing the cursor or named by the active token when specific function signatures
 * or manual pages are unavailable.
 */
class class_summary_provider : public inspector_provider
{
      public:
	class_summary_provider() = default;
	~class_summary_provider() override = default;

	std::string_view name() const noexcept override { return "class_summary"; }

	inspector_result query(const inspector_query &q) override;
	void query_async(const inspector_query &q, std::function<void(inspector_result)> callback) override;

	void clear_cache();

	static std::string format_summary_markdown(const tools::class_summary_info &summary, int available_lines,
						   int available_cols, std::string_view focused_token = "");
	static std::string format_summary_markdown(const tools::class_summary_info &summary, int available_lines,
						   std::string_view focused_token)
	{
		return format_summary_markdown(summary, available_lines, 80, focused_token);
	}
	static std::string format_summary_markdown(const tools::class_summary_info &summary, int available_lines)
	{
		return format_summary_markdown(summary, available_lines, 80, "");
	}

      private:
	std::string make_cache_key(const inspector_query &q) const;
	std::optional<cached_class_summary> lookup_cache(const std::string &key);
	void store_cache(const std::string &key, const cached_class_summary &entry);

	/**
	 * @brief Mutex protecting the in-memory class summary lookup cache.
	 *
	 * Protects:
	 * - cache_ (unordered_map storing positive and negative cached class summaries)
	 *
	 * Locking guidelines:
	 * - Acquired briefly in lookup_cache(), store_cache(), and clear_cache().
	 * - Never hold this mutex across I/O, LSP calls, or thread dispatches.
	 */
	mutable std::mutex cache_mutex_;
	std::unordered_map<std::string, cached_class_summary> cache_;
};

} // namespace turbostar
