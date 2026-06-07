#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
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
	bool load_weights(const std::string &custom_path = "");

	/**
	 * @brief Evaluates whether a transition from previous turn to current turn is a milestone boundary.
	 * @param text_prev Turn T-1 context (prompt + response conclusion).
	 * @param text_curr Turn T prompt.
	 * @param metadata 16-dimensional metadata vector M.
	 * @return Probability of the transition being a boundary in range [0.0, 1.0]. Returns negative value on failure.
	 */
	float predict_boundary(const std::string &text_prev, const std::string &text_curr, const std::vector<float> &metadata);

	/**
	 * @brief Checks if weights are currently loaded.
	 */
	bool is_loaded() const
	{
		return weights_.loaded;
	}

	// Exposed utilities for testing
	static uint32_t compute_crc32(const std::string &str);
	static std::vector<std::string> tokenize(const std::string &text);
	static std::vector<float> pool_text(const std::vector<std::string> &tokens, const float *embed_matrix);

      private:
	context_dnn() = default;
	dnn_weights weights_;
};

} // namespace turbostar
