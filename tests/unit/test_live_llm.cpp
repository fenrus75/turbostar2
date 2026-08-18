// Tested source file: src/agentlib/protocols/openai_response_connection.h, src/agentlib/httplib_transport.cpp
#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include "../../src/agentlib/httplib_transport.h"
#include "../../src/agentlib/ai_model.h"
#include "../../src/agentlib/llm_client.h"
#include "../../src/agentlib/llm_types.h"


#ifndef LIVE_LLM_TESTS_ENABLED
#define LIVE_LLM_TESTS_ENABLED 0
#endif

#ifndef LIVE_LLM_URL
#define LIVE_LLM_URL "http://localhost:8000/v1"
#endif

#ifndef LIVE_LLM_MODEL
#define LIVE_LLM_MODEL "default"
#endif

int main()
{
	test_watchdog::setup_watchdog(30);

	std::string server_url = LIVE_LLM_URL;
	const char *env_url = std::getenv("TURBOSTAR_LIVE_LLM_URL");
	if (!env_url || !*env_url) {
		env_url = std::getenv("OPENAI_API_BASE");
	}
	if (env_url && *env_url) {
		server_url = env_url;
	}

	const char *env_enabled = std::getenv("TURBOSTAR_ENABLE_LIVE_LLM_TESTS");
	bool enabled = (LIVE_LLM_TESTS_ENABLED != 0) || (env_enabled && std::string(env_enabled) == "1") || (env_url != nullptr);

	if (!enabled) {
		std::cout << "[SKIPPED] Live LLM tests disabled. Enable via Meson option (-Dlive-llm-tests=true) or env (TURBOSTAR_LIVE_LLM_URL).\n";
		return 77; // Meson skip exit code
	}

	std::cout << "Testing live LLM server connection at: " << server_url << std::endl;

	std::string error_out;
	auto models = agentlib::fetch_models_from_server(server_url, error_out);


	if (models.empty()) {
		std::cout << "[SKIPPED] Live LLM server at " << server_url << " returned no models or is unreachable: " << error_out << std::endl;
		return 77; // Meson skip exit code
	}

	std::cout << "Successfully connected to live LLM server! Available models:\n";
	for (const auto &m : models) {
		std::cout << "  - " << m->get_name() << " (" << m->get_id() << ")\n";
	}

	assert(!models.empty());
	std::string target_model = models.front()->get_id();

	std::cout << "Sending live completion query to model: " << target_model << std::endl;

	auto transport = std::make_shared<agentlib::httplib_transport>(server_url);
	agentlib::llm_client client(transport, target_model, agentlib::api_type::openai);

	std::vector<agentlib::message> convo;
	agentlib::message user_msg;
	user_msg.role = "user";
	user_msg.content = "Hello! Please reply with the single word: PONG";
	convo.push_back(user_msg);

	auto response = client.send_chat(convo);

	std::cout << "Received response from model:\n" << response.msg.content << std::endl;
	assert(!response.msg.content.empty());

	std::cout << "Live LLM integration test passed successfully!\n";
	return 0;
}


