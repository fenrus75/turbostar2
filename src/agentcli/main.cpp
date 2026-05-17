#include <iostream>
#include <cstdlib>
#include <memory>
#include "../agentlib/llm_client.h"
#include "../agentlib/tool_registry.h"
#include "../agentlib/httplib_transport.h"
#include "../agentlib/recording_transport.h"
#include "../agentlib/replay_transport.h"

using namespace agentlib;
using json = nlohmann::json;

int main(int argc, char** argv) {
    std::string prompt = (argc > 1) ? argv[1] : "How cold is it outside in San Francisco, CA?";
    
    const char* env_url = std::getenv("LLM_URL");
    std::string url = env_url ? env_url : "http://192.168.1.42:8080";
    
    // Set up the transport chain
#if defined(LLM_TRANSPORT_REPLAY)
    std::cout << "[Using Replay Transport]" << std::endl;
    auto player = std::make_shared<replay_transport>("llm_debug_traffic.json");
    llm_client client(player);
#elif defined(LLM_TRANSPORT_RECORD)
    std::cout << "[Using Recording Transport]" << std::endl;
    auto http_transport = std::make_shared<httplib_transport>(url);
    auto recorder = std::make_shared<recording_transport>(http_transport, "llm_debug_traffic.json");
    llm_client client(recorder);
#else
    std::cout << "[Using Standard HTTP Transport]" << std::endl;
    auto http_transport = std::make_shared<httplib_transport>(url);
    llm_client client(http_transport);
#endif
    
    tool_registry& registry = tool_registry::get_instance();
    tool_context ctx;
    ctx.fs_security.set_working_directory(std::filesystem::current_path());
    ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::read);

    // 2. We ask a question that triggers the tool
    prompt = (argc > 1) ? argv[1] : "Please find all instances of 'agent_window' in src/editor.h using fs_regexp_lines.";
    std::cout << "Connecting to: " << url << std::endl;
    std::cout << "Prompt: " << prompt << "\n" << std::endl;
    
    std::vector<message> conversation;
    
    message system_msg;
    system_msg.role = "system";
    system_msg.content = "You are a helpful assistant. You must use the provided fs_regexp_lines tool if the user asks to search for something in a file.";
    conversation.push_back(system_msg);

    message user_msg;
    user_msg.role = "user";
    user_msg.content = prompt;
    conversation.push_back(user_msg);

    while (true) {
        message response = client.send_chat(conversation, &registry);

        if (response.tool_calls && !response.tool_calls->empty()) {
            std::cout << "LLM requested tool calls." << std::endl;
            // The LLM's assistant message needs to be added to the history
            conversation.push_back(response);

            for (const auto& call : *response.tool_calls) {
                std::cout << "Executing tool: " << call.function.name << std::endl;
                
                // 3. We now pass the tool_context down to the execution layer
                std::string tool_result = registry.execute_tool(call.function.name, call.function.arguments, ctx);
                
                std::cout << "[Tool Result] " << tool_result << std::endl;

                message tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = tool_result;
                tool_msg.name = call.function.name;
                tool_msg.tool_call_id = call.id;
                
                conversation.push_back(tool_msg);
            }
            // Loop back to send the tool result(s) to the LLM
        } else {
            std::cout << "\nLLM Final Response:\n" << response.content << std::endl;
            break;
        }
    }

    return 0;
}
