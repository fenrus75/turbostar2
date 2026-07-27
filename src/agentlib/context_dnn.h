#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace turbostar
{

struct dnn_weights {
	const float *embedding_matrix = nullptr;
	const float *fc1_weight = nullptr;
	const float *fc1_bias = nullptr;
	const float *fc2_weight = nullptr;
	const float *fc2_bias = nullptr;
	const float *fc3_weight = nullptr;
	const float *fc3_bias = nullptr;
	const float *fc4_weight = nullptr;
	const float *fc4_bias = nullptr;

	void *mmap_addr = nullptr;
	size_t mmap_size = 0;
	bool loaded = false;
};

class context_dnn
{
      public:
	static context_dnn &get_instance();
	~context_dnn();

	/**
	 * @brief Attempts to load weights from the specified path or default fallback paths.
	 * @return true if weights were successfully loaded, false otherwise.
	 */
	bool load_weights(std::string_view custom_path = "");

	/**
	 * @brief Evaluates whether a transition from previous turn to current turn is a milestone boundary.
	 * @param text_prev Turn T-1 context (prompt + response conclusion).
	 * @param text_curr Turn T prompt.
	 * @param metadata 16-dimensional metadata vector M.
	 * @return Probability of the transition being a boundary in range [0.0, 1.0]. Returns negative value on failure.
	 */
	float predict_boundary(std::string_view text_prev, std::string_view text_curr, std::span<const float> metadata);

	/**
	 * @brief Checks if weights are currently loaded.
	 */
	bool is_loaded() const
	{
		return weights_.loaded;
	}

	// Exposed utilities for testing
	static uint32_t compute_crc32(std::string_view str);
	static std::vector<std::string> tokenize(std::string_view text);
	static std::vector<float> pool_text(std::span<const std::string> tokens, const float *embed_matrix);

      private:
	context_dnn() = default;
	dnn_weights weights_;
};

} // namespace turbostar
