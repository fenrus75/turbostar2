import json
import sys
import argparse

def analyze_chat(filepath, turn=None):
    try:
        with open(filepath, 'r') as f:
            data = json.load(f)
    except Exception as e:
        print(f"Error loading {filepath}: {e}")
        return

    conversation = data.get('conversation', [])
    
    if turn is not None:
        if turn < 0 or turn >= len(conversation):
            print(f"Error: Turn {turn} is out of bounds (0 to {len(conversation) - 1})")
            return
        
        # Dump the raw JSON for the specific turn
        print(json.dumps(conversation[turn], indent=4))
        return

    print(f"=== Agent Chat Analysis: {data.get('agent_name')} (ID: {data.get('agent_id')}) ===")
    print("-" * 60)
    
    for i, msg in enumerate(conversation):
        role = msg.get('role', 'unknown')
        content = msg.get('content', '')
        
        if role == 'system':
            print(f"[{i:03d}] SYSTEM: ({len(content)} bytes of system prompt)")
        elif role == 'user':
            preview = content[:150].replace('\n', ' ')
            print(f"[{i:03d}] USER: {preview}{'...' if len(content) > 150 else ''}")
        elif role == 'assistant':
            tool_calls = msg.get('tool_calls', [])
            if tool_calls:
                for tc in tool_calls:
                    func = tc.get('function', {})
                    name = func.get('name', 'unknown')
                    args = func.get('arguments', '')
                    preview = args[:150].replace('\n', ' ')
                    print(f"[{i:03d}] ASSISTANT -> TOOL CALL: {name}")
                    print(f"      Args: {preview}{'...' if len(args) > 150 else ''}")
            else:
                preview = content[:150].replace('\n', ' ')
                print(f"[{i:03d}] ASSISTANT: {preview}{'...' if len(content) > 150 else ''}")
        elif role == 'tool':
            name = msg.get('name', 'unknown')
            status = "SUCCESS"
            if "Error:" in content or "Violation:" in content or "Error parsing" in content:
                status = "FAILED"
            print(f"[{i:03d}] TOOL RESULT ({name}) -> {status}")
            if status == "FAILED":
                preview = content[:250].replace('\n', ' ')
                print(f"      Reason: {preview}{'...' if len(content) > 250 else ''}")
            elif len(content) < 100:
                preview = content.replace('\n', ' ')
                print(f"      {preview}")
            else:
                print(f"      ({len(content)} bytes returned)")
        else:
            print(f"[{i:03d}] {role.upper()}: {content[:50]}...")
        print("-" * 60)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Analyze Turbostar Agent JSON chat logs")
    parser.add_argument("path", help="Path to the JSON log file (e.g. agent_chat_2.json)")
    parser.add_argument("--turn", type=int, help="Dump the raw JSON of a specific turn number", default=None)
    args = parser.parse_args()
    
    analyze_chat(args.path, args.turn)