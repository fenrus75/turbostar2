#include "context_dnn.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <immintrin.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../event_logger.h"

namespace fs = std::filesystem;

namespace turbostar
{

context_dnn &context_dnn::get_instance()
{
	static context_dnn instance;
	return instance;
}

context_dnn::~context_dnn()
{
	if (weights_.loaded && weights_.mmap_addr) {
		::munmap(weights_.mmap_addr, weights_.mmap_size);
	}
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

struct alignas(8) weights_header {
	char magic[8]; // "TSDNNWGT"
	uint32_t version;
	uint32_t embed_rows;
	uint32_t embed_cols;
	uint32_t fc1_out;
	uint32_t fc1_in;
	uint32_t fc2_out;
	uint32_t fc2_in;
	uint32_t fc3_out;
	uint32_t fc3_in;
	uint32_t fc4_out;
	uint32_t fc4_in;
	uint32_t reserved; // padding to align to 8 bytes
};

bool context_dnn::load_weights(const std::string &custom_path)
{
	std::vector<std::string> search_paths;
	if (!custom_path.empty()) {
		search_paths.push_back(expand_user_path(custom_path));
	} else {
		search_paths.push_back("./dnn_training/weights.bin");
		search_paths.push_back("../dnn_training/weights.bin");
		search_paths.push_back(expand_user_path("~/.cache/turbostar/weights.bin"));
		search_paths.push_back(expand_user_path("~/.turbostar/weights.bin"));
	}

	std::string resolved_path;
	for (const auto &p : search_paths) {
		if (fs::exists(p)) {
			resolved_path = p;
			break;
		}
	}

	if (resolved_path.empty()) {
		event_logger::get_instance().log("DNN context classifier weights file not found in search paths.");
		return false;
	}

	int fd = ::open(resolved_path.c_str(), O_RDONLY);
	if (fd < 0) {
		event_logger::get_instance().log("Failed to open weights file: {}", resolved_path);
		return false;
	}

	struct stat sb;
	if (::fstat(fd, &sb) < 0) {
		event_logger::get_instance().log("Failed to stat weights file: {}", resolved_path);
		::close(fd);
		return false;
	}

	size_t file_size = sb.st_size;
	if (file_size < sizeof(weights_header)) {
		event_logger::get_instance().log("Weights file {} is too small.", resolved_path);
		::close(fd);
		return false;
	}

	void *addr = ::mmap(nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0);
	::close(fd);

	if (addr == MAP_FAILED) {
		event_logger::get_instance().log("Failed to mmap weights file: {}", resolved_path);
		return false;
	}

	const weights_header *header = static_cast<const weights_header *>(addr);

	if (std::memcmp(header->magic, "TSDNNWGT", 8) != 0) {
		event_logger::get_instance().log("Weights file {} has invalid magic.", resolved_path);
		::munmap(addr, file_size);
		return false;
	}

	if (header->version != 1) {
		event_logger::get_instance().log("Weights file {} has unsupported version: {}", resolved_path, header->version);
		::munmap(addr, file_size);
		return false;
	}

	// Basic validation of dimensions
	if (header->embed_rows != 1024 || header->embed_cols != 128 || header->fc1_out != 256 || header->fc1_in != 1040 ||
	    header->fc2_out != 128 || header->fc2_in != 256 || header->fc3_out != 64 || header->fc3_in != 128 || header->fc4_out != 2 ||
	    header->fc4_in != 64) {
		event_logger::get_instance().log("Weights file {} has incorrect dimensions.", resolved_path);
		::munmap(addr, file_size);
		return false;
	}

	// Calculate float array start and total size
	const float *float_data = reinterpret_cast<const float *>(reinterpret_cast<const char *>(addr) + sizeof(weights_header));
	size_t expected_floats = 1024 * 128 + 256 * 1040 + 256 + 128 * 256 + 128 + 64 * 128 + 64 + 2 * 64 + 2;

	if (file_size < sizeof(weights_header) + expected_floats * sizeof(float)) {
		event_logger::get_instance().log("Weights file {} is truncated.", resolved_path);
		::munmap(addr, file_size);
		return false;
	}

	// Unmap previous if already loaded (shouldn't happen under standard flow)
	if (weights_.loaded && weights_.mmap_addr) {
		::munmap(weights_.mmap_addr, weights_.mmap_size);
	}

	weights_.mmap_addr = addr;
	weights_.mmap_size = file_size;

	weights_.embedding_matrix = float_data;
	weights_.fc1_weight = float_data + 131072;
	weights_.fc1_bias = float_data + 131072 + 266240;
	weights_.fc2_weight = float_data + 131072 + 266240 + 256;
	weights_.fc2_bias = float_data + 131072 + 266240 + 256 + 32768;
	weights_.fc3_weight = float_data + 131072 + 266240 + 256 + 32768 + 128;
	weights_.fc3_bias = float_data + 131072 + 266240 + 256 + 32768 + 128 + 8192;
	weights_.fc4_weight = float_data + 131072 + 266240 + 256 + 32768 + 128 + 8192 + 64;
	weights_.fc4_bias = float_data + 131072 + 266240 + 256 + 32768 + 128 + 8192 + 64 + 128;

	weights_.loaded = true;
	event_logger::get_instance().log("Successfully loaded Milestone Boundary DNN weights from {}", resolved_path);
	return true;
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

std::vector<float> context_dnn::pool_text(const std::vector<std::string> &tokens, const float *embed_matrix)
{
	std::vector<float> pooled(512, 0.0f);
	if (tokens.empty() || !embed_matrix) {
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
			const float *emb = embed_matrix + hash_idx * 128;
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

static std::vector<float> evaluate_dense_layer(const std::vector<float> &input, const float *weights, const float *biases, size_t out_dim,
					       size_t in_dim)
{
	std::vector<float> output(out_dim);
	size_t input_size = input.size();
	for (size_t i = 0; i < out_dim; ++i) {
		const float *w_row = weights + i * in_dim;
		__m128 acc0 = _mm_setzero_ps();
		__m128 acc1 = _mm_setzero_ps();
		__m128 acc2 = _mm_setzero_ps();
		__m128 acc3 = _mm_setzero_ps();

		size_t j = 0;
		// Process 16 elements per iteration using 4 SSE registers to break dependency chain
		for (; j + 15 < input_size; j += 16) {
			__m128 in0 = _mm_loadu_ps(&input[j + 0]);
			__m128 w0 = _mm_loadu_ps(&w_row[j + 0]);
			acc0 = _mm_add_ps(acc0, _mm_mul_ps(in0, w0));

			__m128 in1 = _mm_loadu_ps(&input[j + 4]);
			__m128 w1 = _mm_loadu_ps(&w_row[j + 4]);
			acc1 = _mm_add_ps(acc1, _mm_mul_ps(in1, w1));

			__m128 in2 = _mm_loadu_ps(&input[j + 8]);
			__m128 w2 = _mm_loadu_ps(&w_row[j + 8]);
			acc2 = _mm_add_ps(acc2, _mm_mul_ps(in2, w2));

			__m128 in3 = _mm_loadu_ps(&input[j + 12]);
			__m128 w3 = _mm_loadu_ps(&w_row[j + 12]);
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
	std::vector<float> h1 = evaluate_dense_layer(input, weights_.fc1_weight, weights_.fc1_bias, 256, 1040);
	std::vector<float> h2 = evaluate_dense_layer(h1, weights_.fc2_weight, weights_.fc2_bias, 128, 256);
	std::vector<float> h3 = evaluate_dense_layer(h2, weights_.fc3_weight, weights_.fc3_bias, 64, 128);

	// fc4 output (2 nodes, linear + softmax)
	std::vector<float> logits(2, 0.0f);
	for (size_t i = 0; i < 2; ++i) {
		logits[i] = weights_.fc4_bias[i];
		const float *w_row = weights_.fc4_weight + i * 64;
		for (size_t j = 0; j < h3.size(); ++j) {
			logits[i] += h3[j] * w_row[j];
		}
	}

	// Softmax
	float max_logit = std::max(logits[0], logits[1]);
	float e0 = std::exp(logits[0] - max_logit);
	float e1 = std::exp(logits[1] - max_logit);
	float sum_e = e0 + e1;
	float probability = e0 / sum_e;

	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

	event_logger::get_instance().log("Milestone classifier evaluated in {} us (prob={:.4f})", duration_us, probability);

	return probability;
}

} // namespace turbostar
