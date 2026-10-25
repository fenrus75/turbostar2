#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class document;

/**
 * @brief Represents a context query for companion window content providers.
 */
struct inspector_query {
	std::string file_path;
	int line{0};
	int col{0};
	std::string token;
	std::string line_text;
	uint64_t request_id{0};
};

/**
 * @brief Represents the scored response returned by an inspector content provider.
 */
struct inspector_result {
	std::string provider_name;
	int score{0};         // 0 - 100 quality/relevance score
	std::string markdown; // Markdown Lite formatted string
	bool is_final{true};  // True if final; false if asynchronous resolution is pending
};

/*

# subclasses of inspector_provider

| subclass                    | filename                    |
| --------------------------- | --------------------------- | 
| fallback_inspector_provider | src/ui/inspector_provider.h |
| lsp_inspector_provider      | src/ui/inspector_provider.h |

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

/**
 * @brief Basic fallback provider displaying the current token or an idle notice.
 */
class fallback_inspector_provider : public inspector_provider
{
      public:
	std::string_view name() const noexcept override { return "token"; }
	inspector_result query(const inspector_query &q) override;
};

/**
 * @brief LSP and code analysis provider for function signatures, arguments, and declarations.
 */
class lsp_inspector_provider : public inspector_provider
{
      public:
	explicit lsp_inspector_provider(std::function<document *()> active_doc_fn = nullptr);
	~lsp_inspector_provider() override = default;

	std::string_view name() const noexcept override { return "lsp"; }
	inspector_result query(const inspector_query &q) override;
	void query_async(const inspector_query &q, std::function<void(inspector_result)> callback) override;

      private:
	std::function<document *()> active_doc_fn_;
};

/**
 * @brief Coordinates registered inspector providers, scoring, ranking, and lifecycle.
 */
class inspector_coordinator
{
      public:
	inspector_coordinator() = default;
	~inspector_coordinator() = default;

	void register_provider(std::unique_ptr<inspector_provider> provider);

	// Synchronously queries all providers, returns highest score result,
	// and dispatches query_async for providers with is_final == false.
	inspector_result query(const inspector_query &q, std::function<void(inspector_result)> async_callback);

	uint64_t next_request_id() noexcept { return ++current_request_id_; }
	uint64_t current_request_id() const noexcept { return current_request_id_.load(); }

      private:
	/*
	 * mutex_ protects providers_ list during registration and query dispatch.
	 * Locking Rules:
	 * - Held briefly while modifying or snapshotting the provider registry.
	 */
	mutable std::mutex mutex_;
	std::vector<std::unique_ptr<inspector_provider>> providers_;
	std::atomic<uint64_t> current_request_id_{0};
};
