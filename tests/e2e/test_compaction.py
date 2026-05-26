import os
import json
import subprocess
import tempfile
import pytest

# Helper to run agentcli in replay mode
def run_agentcli(tmp_path, prompt, traffic_json_path, mock_epoch="12345"):
    env = os.environ.copy()
    env["HOME"] = str(tmp_path)
    build_root = os.environ.get('MESON_BUILD_ROOT', 'build')
    cli_path = os.path.join(build_root, 'agentcli_replay')
    
    out_file = os.path.join(tmp_path, "out.json")
    
    cmd = [
        cli_path, 
        "--prompt", prompt, 
        "--replay", traffic_json_path, 
        "--dump-state", out_file,
        "--project-dir", str(tmp_path),
        "--mock-epoch", mock_epoch
    ]
    
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)
    
    if os.path.exists(out_file):
        with open(out_file, 'r') as f:
            return json.load(f), result.stdout
    return None, result.stdout

def create_traffic_file(tmp_path, name, responses):
    path = os.path.join(tmp_path, name)
    data = []
    for resp in responses:
        data.append({
            "request": {},
            "response": {
                "status_code": 200,
                "body": json.dumps({"choices": [{"message": resp}]})
            }
        })
    with open(path, 'w') as f:
        json.dump(data, f)
    return path

def test_ephemeral_error_zapping(tmp_path):
    # Mock sequence: 
    # 1. Agent makes bad tool call
    # 2. Agent makes good tool call (same tool)
    traffic_file = create_traffic_file(tmp_path, "error_traffic.json", [
        {
            "role": "assistant",
            "content": "I will try this.",
            "tool_calls": [{"id": "call_1", "type": "function", "function": {"name": "fs_read_lines", "arguments": "{\"path\":\"bad_file.txt\"}"}}]
        },
        {
            "role": "assistant",
            "content": "Oops, my bad. Let me fix that.",
            "tool_calls": [{"id": "call_2", "type": "function", "function": {"name": "fs_read_lines", "arguments": "{\"path\":\"tests/unit/test_fs_file_size.cpp\"}"}}]
        }
    ])
    
    state, stdout = run_agentcli(tmp_path, "Read a file", traffic_file)
    assert state is not None, "Failed to dump state"
    
    # Verify the stats
    stats = state.get("stats", {})
    assert stats.get("ephemeral_errors_zapped", 0) == 1
    
    # Verify conversation history
    conv = state.get("conversation", [])
    # 1 system, 1 user, 1 assistant (stripped), 1 tool success
    assert len(conv) == 4 
    
    # Check that the failed call is completely gone
    for msg in conv:
        assert "bad_file.txt" not in json.dumps(msg)
        
    # Check that the retry message's content (apology) was stripped
    assert conv[2]["role"] == "assistant"
    assert conv[2]["content"] == "" 
    assert conv[2]["tool_calls"][0]["function"]["name"] == "fs_read_lines"

def test_proactive_page_out(tmp_path):
    traffic_file = create_traffic_file(tmp_path, "pageout_traffic.json", [
        {
            "role": "assistant",
            "content": "Milestone time!",
            "tool_calls": [{"id": "call_1", "type": "function", "function": {"name": "agent_mark_milestone", "arguments": "{\"title\": \"M1\", \"summary\": \"Sum1\"}"}}]
        },
        {
            "role": "assistant",
            "content": "More work...",
            "tool_calls": [{"id": "call_2", "type": "function", "function": {"name": "agent_compress_history", "arguments": "{\"title\": \"M2\", \"summary\": \"Sum2\"}"}}]
        }    ])
    
    state, stdout = run_agentcli(tmp_path, "Do stuff", traffic_file, mock_epoch="12345")
    assert state is not None
    
    stats = state.get("stats", {})
    assert stats.get("context_pages_out", 0) > 0
    
    conv = state.get("conversation", [])
    
    # Verify a pointer message was injected
    has_pointer = False
    for msg in conv:
        if msg.get("role") == "system" and "SYSTEM MEMORY" in msg.get("content", ""):
            has_pointer = True
            break
    assert has_pointer
    
    # Verify files were created on disk
    history_dir = os.path.join(tmp_path, ".cache", "turbostar", "projects")
    # Find the hash dir
    if os.path.exists(history_dir):
        hash_dirs = os.listdir(history_dir)
        if len(hash_dirs) > 0:
            agent_dir = os.path.join(history_dir, hash_dirs[0], "history", "TestAgent")
            assert os.path.exists(os.path.join(agent_dir, "milestone_12345.json"))
            assert os.path.exists(os.path.join(agent_dir, "milestone_12345_metadata.json"))

def test_think_free_restore(tmp_path):
    import hashlib
    # Setup a dummy archive on disk using the correct hash
    # In fs_utils.cpp, the hash is std::hash<std::string> on the repo_root.
    # Python's hash() is salted, so we can't replicate std::hash easily.
    # Instead, we just let agentcli run once to generate the directory structure,
    # then we write our dummy archive into whatever folder it created!
    
    run_agentcli(tmp_path, "Ping", create_traffic_file(tmp_path, "ping.json", []))
    
    history_dir = os.path.join(tmp_path, ".cache", "turbostar", "projects")
    hash_dirs = os.listdir(history_dir) if os.path.exists(history_dir) else []
    assert len(hash_dirs) > 0, "Agent failed to create project cache dir"
    
    agent_dir = os.path.join(history_dir, hash_dirs[0], "history", "TestAgent")
    os.makedirs(agent_dir, exist_ok=True)
    
    archive_data = {
        "milestone_id": "milestone_999",
        "title": "Old Task",
        "conversation": [
            {
                "role": "assistant",
                "content": "I am thinking so hard right now...",
                "tool_calls": [{"id": "call_old", "type": "function", "function": {"name": "fs_read_lines", "arguments": "{}"}}]
            }
        ]
    }
    with open(os.path.join(agent_dir, "milestone_999.json"), "w") as f:
        json.dump(archive_data, f)
    
    # Now, the agent restores it
    traffic_file = create_traffic_file(tmp_path, "restore_traffic.json", [
        {
            "role": "assistant",
            "content": "Restoring!",
            "tool_calls": [{"id": "call_res", "type": "function", "function": {"name": "agent_restore_context", "arguments": "{\"milestone_id\": \"milestone_999\", \"compression_level\": 2}"}}]
        }
    ])
    
    state, stdout = run_agentcli(tmp_path, "Restore", traffic_file)
    assert state is not None
    
    # The restore tool should have stripped the "content" of the loaded assistant message
    conv = state.get("conversation", [])
    found_restored_msg = False
    for msg in conv:
        if msg.get("role") == "assistant" and msg.get("tool_calls") and len(msg["tool_calls"]) > 0 and msg["tool_calls"][0].get("id") == "call_old":
            found_restored_msg = True
            assert msg.get("content") == "", "Think-Free restoration failed: content was not stripped"
            break
            
    assert found_restored_msg, f"Failed to restore context from disk. Conversation: {json.dumps(conv, indent=2)}"
