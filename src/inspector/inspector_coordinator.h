#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "inspector/inspector_provider.h"

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
