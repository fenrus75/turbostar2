#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include "inspector/inspector_provider.h"

struct cached_man_entry {
	bool exists{false};
	std::string signature;
	std::string summary;
	std::string file_path;
};

/**
 * @brief Inspector provider that resolves standard C / POSIX library symbols from system man pages in section 3.
 */
class man_page_provider : public inspector_provider
{
      public:
	explicit man_page_provider(std::string man3_dir = "/usr/share/man/man3");
	~man_page_provider() override = default;

	std::string_view name() const noexcept override { return "man"; }
	inspector_result query(const inspector_query &q) override;
	void query_async(const inspector_query &q, std::function<void(inspector_result)> callback) override;

	// Cache management
	void clear_cache();
	size_t cache_size() const;

	// Helper parsers (public static for testing and reuse)
	static std::string collapse_prototype(std::string_view raw_proto);
	static std::string extract_synopsis_prototype(std::string_view full_md, std::string_view token);
	static std::string extract_short_description(std::string_view full_md, std::string_view token);

      private:
	std::string man3_dir_;

	/*
	 * cache_mutex_ protects the in-memory cache_ map against concurrent read and write access
	 * from the UI thread and async background resolution worker threads.
	 * Locking Rules:
	 * - Held briefly while querying, inserting, or clearing entries in cache_.
	 */
	mutable std::mutex cache_mutex_;
	std::unordered_map<std::string, cached_man_entry> cache_;

	std::optional<cached_man_entry> lookup_cached(const std::string &token) const;
	void store_cached(const std::string &token, const cached_man_entry &entry);
	cached_man_entry parse_man_page(const std::string &file_path, const std::string &token);
	std::string find_man_file(const std::string &token) const;
};
