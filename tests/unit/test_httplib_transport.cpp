#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <httplib.h>
#include "../../src/agentlib/httplib_transport.h"

int main()
{
	// Set up a simple local HTTP server that is slow to respond (e.g. sleeps for 1 second)
	httplib::Server svr;
 
	svr.Post("/slow", [](const httplib::Request&, httplib::Response& res) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		res.set_content("slow response completed", "text/plain");
	});
 
	// Bind to an ephemeral port
	int port = svr.bind_to_any_port("127.0.0.1");
	assert(port > 0);
 
	// Start the server in a background thread
	std::thread server_thread([&]() {
		svr.listen_after_bind();
	});
 
	std::cout << "Test server listening on port " << port << "..." << std::endl;
 
	// Give the server a small moment to spin up
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
 
	// Create httplib_transport pointing to our slow server
	std::string base_url = "http://127.0.0.1:" + std::to_string(port);
	agentlib::httplib_transport transport(base_url);
 
	// Test the standard POST request. We expect this to succeed post-fix (takes 1s, timeout is 300s)
	// but fail pre-fix (takes 1s, timeout is 5s).
	std::cout << "Making POST request to /slow..." << std::endl;
	auto start = std::chrono::steady_clock::now();
	auto resp = transport.post("/slow", "{}");
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();

	std::cout << "POST request completed. Status: " << resp.status_code << ", Body: " << resp.body << std::endl;

	// Test the streaming POST request.
	std::cout << "Making streaming POST request to /slow..." << std::endl;
	std::string stream_body;
	bool stream_success = transport.post_stream("/slow", "{}", [&](const char* data, size_t len, size_t, size_t) {
		stream_body.append(data, len);
		return true;
	});

	std::cout << "Streaming request completed. Success: " << std::boolalpha << stream_success << ", Body: " << stream_body << std::endl;

	// Clean up the server
	svr.stop();
	if (server_thread.joinable()) {
		server_thread.join();
	}

	// Assertions that will fail pre-fix because of the 5-second timeout, but pass post-fix.
	assert(resp.status_code == 200);
	assert(resp.body == "slow response completed");
	assert(stream_success);
	assert(stream_body == "slow response completed");

	std::cout << "httplib_transport timeout test passed successfully!" << std::endl;
	return 0;
}
