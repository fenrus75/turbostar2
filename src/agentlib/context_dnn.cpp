#include "context_dnn.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <immintrin.h>
#include <fstream>
#include <iostream>
#include "../event_logger.h"

namespace fs = std::filesystem;

namespace turbostar {

context_dnn &context_dnn::get_instance()
{
	static context_dnn instance;
	return instance;
}

static std::string expand_user_path(const std::string &path)
{
	if (path.empty() || path[0] != '~') {
		return path;
	}
	const char *home = std::getenv("HOME");
	if (!home) {
		return path.substr(1);
	}
	return std::string(home) + path.substr(1);
}

bool context_dnn::load_weights(const std::string &custom_path)
{
	std::vector<std::string> search_paths;
	if (!custom_path.empty()) {
		search_paths.push_back(expand_user_path(custom_path));
	} else {
		search_paths.push_back("./dnn_training/weights.json");
		search_paths.push_back("../dnn_training/weights.json");
		search_paths.push_back(expand_user_path("~/.cache/turbostar/weights.json"));
		search_paths.push_back(expand_user_path("~/.turbostar/weights.json"));
	}

	std::string resolved_path;
	for (const auto &p : search_paths) {
		if (fs::exists(p)) {
			resolved_path = p;
			break;
		}
	}

	if (resolved_path.empty()) {
		event_logger::get_instance().log("DNN context classifier weights file not found in search paths. Falling back to heuristic.");
		return false;
	}

	try {
		std::ifstream file(resolved_path);
		if (!file.is_open()) {
			event_logger::get_instance().log("Failed to open weights file: {}", resolved_path);
			return false;
		}

		nlohmann::json root = nlohmann::json::parse(file);

		weights_.embedding_matrix = root.at("embedding_matrix").get<std::vector<std::vector<float>>>();
		weights_.fc1_weight = root.at("fc1_weight").get<std::vector<std::vector<float>>>();
		weights_.fc1_bias = root.at("fc1_bias").get<std::vector<float>>();
		weights_.fc2_weight = root.at("fc2_weight").get<std::vector<std::vector<float>>>();
		weights_.fc2_bias = root.at("fc2_bias").get<std::vector<float>>();
		weights_.fc3_weight = root.at("fc3_weight").get<std::vector<std::vector<float>>>();
		weights_.fc3_bias = root.at("fc3_bias").get<std::vector<float>>();
		weights_.fc4_weight = root.at("fc4_weight").get<std::vector<std::vector<float>>>();
		weights_.fc4_bias = root.at("fc4_bias").get<std::vector<float>>();

		// Basic validation of dimensions
		if (weights_.embedding_matrix.size() != 1024 || weights_.embedding_matrix[0].size() != 128 ||
		    weights_.fc1_weight.size() != 256 || weights_.fc1_weight[0].size() != 1040 ||
		    weights_.fc1_bias.size() != 256 ||
		    weights_.fc2_weight.size() != 128 || weights_.fc2_weight[0].size() != 256 ||
		    weights_.fc2_bias.size() != 128 ||
		    weights_.fc3_weight.size() != 64 || weights_.fc3_weight[0].size() != 128 ||
		    weights_.fc3_bias.size() != 64 ||
		    weights_.fc4_weight.size() != 1 || weights_.fc4_weight[0].size() != 64 ||
		    weights_.fc4_bias.size() != 1) {
			event_logger::get_instance().log("Weights file {} loaded but has incorrect tensor dimensions.", resolved_path);
			return false;
		}

		weights_.loaded = true;
		event_logger::get_instance().log("Successfully loaded Milestone Boundary DNN weights from {}", resolved_path);
		return true;
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Error parsing context DNN weights {}: {}", resolved_path, e.what());
		return false;
	}
}

uint32_t context_dnn::compute_crc32(const std::string &str)
{
	uint32_t crc = 0xFFFFFFFF;
	for (char c : str) {
		crc ^= static_cast<uint8_t>(c);
		for (int i = 0; i < 8; ++i) {
			if (crc & 1) {
				crc = (crc >> 1) ^ 0xEDB88320;
			} else {
				crc >>= 1;
			}
		}
	}
	return ~crc;
}

std::vector<std::string> context_dnn::tokenize(const std::string &text)
{
	std::vector<std::string> tokens;
	std::string current;
	for (char c : text) {
		if (std::isalnum(static_cast<unsigned char>(c))) {
			current += std::tolower(static_cast<unsigned char>(c));
		} else {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
		}
	}
	if (!current.empty()) {
		tokens.push_back(current);
	}
	return tokens;
}

std::vector<float> context_dnn::pool_text(const std::vector<std::string> &tokens, const std::vector<std::vector<float>> &embed_matrix)
{
	std::vector<float> pooled(512, 0.0f);
	if (tokens.empty()) {
		return pooled;
	}

	size_t L = tokens.size();
	for (int q = 0; q < 4; ++q) {
		size_t start = (q * L) / 4;
		size_t end = ((q + 1) * L) / 4;
		if (end <= start) {
			end = start + 1;
		}
		if (end > L) {
			end = L;
		}

		std::vector<float> sum(128, 0.0f);
		size_t count = end - start;
		for (size_t i = start; i < end; ++i) {
			uint32_t hash_idx = compute_crc32(tokens[i]) % 1024;
			const auto &emb = embed_matrix[hash_idx];
			for (int d = 0; d < 128; ++d) {
				sum[d] += emb[d];
			}
		}

		float inv_count = 1.0f / static_cast<float>(count);
		for (int d = 0; d < 128; ++d) {
			pooled[q * 128 + d] = sum[d] * inv_count;
		}
	}
	return pooled;
}

static std::vector<float> evaluate_dense_layer(const std::vector<float> &input,
					       const std::vector<std::vector<float>> &weights,
					       const std::vector<float> &biases)
{
	std::vector<float> output(weights.size());
	size_t input_size = input.size();
	for (size_t i = 0; i < weights.size(); ++i) {
		const auto &w_row = weights[i];
		__m128 acc0 = _mm_setzero_ps();
		__m128 acc1 = _mm_setzero_ps();
		__m128 acc2 = _mm_setzero_ps();
		__m128 acc3 = _mm_setzero_ps();

		size_t j = 0;
		// Process 16 elements per iteration using 4 SSE registers to break dependency chain
		for (; j + 15 < input_size; j += 16) {
			__m128 in0 = _mm_loadu_ps(&input[j + 0]);
			__m128 w0  = _mm_loadu_ps(&w_row[j + 0]);
			acc0 = _mm_add_ps(acc0, _mm_mul_ps(in0, w0));

			__m128 in1 = _mm_loadu_ps(&input[j + 4]);
			__m128 w1  = _mm_loadu_ps(&w_row[j + 4]);
			acc1 = _mm_add_ps(acc1, _mm_mul_ps(in1, w1));

			__m128 in2 = _mm_loadu_ps(&input[j + 8]);
			__m128 w2  = _mm_loadu_ps(&w_row[j + 8]);
			acc2 = _mm_add_ps(acc2, _mm_mul_ps(in2, w2));

			__m128 in3 = _mm_loadu_ps(&input[j + 12]);
			__m128 w3  = _mm_loadu_ps(&w_row[j + 12]);
			acc3 = _mm_add_ps(acc3, _mm_mul_ps(in3, w3));
		}

		// Scalar tail fallback (no-op since sizes are multiples of 16, but kept for correctness)
		float s0 = biases[i];
		for (; j < input_size; ++j) {
			s0 += input[j] * w_row[j];
		}

		// Combine the 4 SSE accumulators
		__m128 acc_sum = _mm_add_ps(_mm_add_ps(acc0, acc1), _mm_add_ps(acc2, acc3));

		// Horizontal reduction of the 4 floats in acc_sum
		__m128 shuf = _mm_shuffle_ps(acc_sum, acc_sum, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sums = _mm_add_ps(acc_sum, shuf);
		shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(1, 0, 3, 2));
		sums = _mm_add_ps(sums, shuf);
		float sum = _mm_cvtss_f32(sums) + s0;

		if (sum > 0.0f) {
			output[i] = sum;
		} else {
			output[i] = 0.01f * sum;
		}
	}
	return output;
}

static float run_sigmoid(float val)
{
	return 1.0f / (1.0f + std::exp(-val));
}

float context_dnn::predict_boundary(const std::string &text_prev, const std::string &text_curr, const std::vector<float> &metadata)
{
	if (!weights_.loaded) {
		return -1.0f;
	}

	auto start_time = std::chrono::high_resolution_clock::now();

	// 1. Data preprocessing: Tokenize & pool
	std::vector<std::string> tokens_prev = tokenize(text_prev);
	std::vector<std::string> tokens_curr = tokenize(text_curr);

	std::vector<float> v_prev = pool_text(tokens_prev, weights_.embedding_matrix);
	std::vector<float> v_curr = pool_text(tokens_curr, weights_.embedding_matrix);

	// 2. Assemble input vector [V_prev, V_curr, Metadata] (1040-dim)
	std::vector<float> input;
	input.reserve(1040);
	input.insert(input.end(), v_prev.begin(), v_prev.end());
	input.insert(input.end(), v_curr.begin(), v_curr.end());
	input.insert(input.end(), metadata.begin(), metadata.end());

	if (input.size() != 1040) {
		return -1.0f;
	}

	// 3. Forward pass layers
	std::vector<float> h1 = evaluate_dense_layer(input, weights_.fc1_weight, weights_.fc1_bias);
	std::vector<float> h2 = evaluate_dense_layer(h1, weights_.fc2_weight, weights_.fc2_bias);
	std::vector<float> h3 = evaluate_dense_layer(h2, weights_.fc3_weight, weights_.fc3_bias);

	// fc4 output (single node sigmoid)
	float output_val = weights_.fc4_bias[0];
	for (size_t i = 0; i < h3.size(); ++i) {
		output_val += h3[i] * weights_.fc4_weight[0][i];
	}
	float probability = run_sigmoid(output_val);

	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

	event_logger::get_instance().log("Milestone classifier evaluated in {} us (prob={:.4f})", duration_us, probability);

	return probability;
}

} // namespace turbostar
