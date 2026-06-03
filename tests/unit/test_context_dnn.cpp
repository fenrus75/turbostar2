#include <cassert>
#include <iostream>
#include "../../src/agentlib/context_dnn.h"

int main()
{
	using namespace turbostar;

	// 1. Test standard CRC32 computations matching Python's binascii.crc32
	// "hello" -> 0xF7D189B4
	uint32_t hello_crc = context_dnn::compute_crc32("hello");
	std::cout << "hello_crc: 0x" << std::hex << hello_crc << std::dec << std::endl;
	assert(hello_crc == 0xF7D189B4);

	// "world" -> 0x3D116035
	uint32_t world_crc = context_dnn::compute_crc32("world");
	assert(world_crc == 0x3D116035);

	// "turbostar" -> 0x4296716E
	uint32_t turbostar_crc = context_dnn::compute_crc32("turbostar");
	assert(turbostar_crc == 0x4296716E);

	// 2. Test alphanumeric lowercased Tokenizer
	std::string raw_text = "Hello, World! 123... Turbostar-editor.";
	std::vector<std::string> tokens = context_dnn::tokenize(raw_text);

	assert(tokens.size() == 5);
	assert(tokens[0] == "hello");
	assert(tokens[1] == "world");
	assert(tokens[2] == "123");
	assert(tokens[3] == "turbostar");
	assert(tokens[4] == "editor");

	// 3. Test Regional Pooling dimensions and values
	std::vector<std::vector<float>> embed_matrix(1024, std::vector<float>(128, 0.5f));
	std::vector<float> pooled = context_dnn::pool_text(tokens, embed_matrix);
	assert(pooled.size() == 512);
	for (float val : pooled) {
		assert(std::abs(val - 0.5f) < 1e-5f);
	}

	// 4. Test loading weights from local dnn_training/weights.json
	auto &dnn = context_dnn::get_instance();
	bool load_ok = dnn.load_weights("./dnn_training/weights.json");
	assert(load_ok);
	assert(dnn.is_loaded());

	// 5. Run boundary prediction with dummy test context
	std::string text_prev = "implement boundary classifier unit test [Agent Conclusion: ] completed boundary test logic successfully";
	std::string text_curr = "can we verify that the dnn weights compile correctly?";
	std::vector<float> metadata(16, 0.0f);
	// simulate warning token pressure level (idx 7 = 1.0) and git_commit completed (idx 10 = 1.0)
	metadata[7] = 1.0f;
	metadata[10] = 1.0f;

	float prob = dnn.predict_boundary(text_prev, text_curr, metadata);
	std::cout << "Prediction boundary probability: " << prob << std::endl;
	assert(prob >= 0.0f && prob <= 1.0f);

	std::cout << "Milestone Boundary C++ DNN unit tests passed successfully!\n";
	return 0;
}
