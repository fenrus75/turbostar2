#!/usr/bin/env python3
import urllib.request
import urllib.parse
import json
import time
import sys

BASE_URL = "http://localhost:7820/a2a/v1"
AGENT_NAME = "research"

def main():
    instructions = "Print 'Hello A2A World!' and list current directory contents."
    if len(sys.argv) > 1:
        instructions = " ".join(sys.argv[1:])

    print(f"[*] Dispatching task to {AGENT_NAME} agent with prompt: '{instructions}'...")
    
    # 1. Create a task
    url = f"{BASE_URL}/agents/{AGENT_NAME}/tasks"
    payload = {
        "instructions": instructions
    }
    
    data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'}, method='POST')
    
    try:
        with urllib.request.urlopen(req) as response:
            res_body = response.read().decode('utf-8')
            res_json = json.loads(res_body)
            task_id = res_json.get("task_id")
            print(f"[+] Task created successfully! Task ID: {task_id}")
    except Exception as e:
        print(f"[-] Failed to create task: {e}")
        if hasattr(e, 'read'):
            print(e.read().decode('utf-8'))
        sys.exit(1)

    # 2. Poll for status
    print(f"[*] Polling status for task {task_id}...")
    status_url = f"{BASE_URL}/tasks/{task_id}"
    
    while True:
        req = urllib.request.Request(status_url, method='GET')
        try:
            with urllib.request.urlopen(req) as response:
                status_body = response.read().decode('utf-8')
                status_json = json.loads(status_body)
                
                status = status_json.get("status", "unknown")
                print(f"    -> Current status: {status}")
                
                if status in ["success", "completed", "failure", "failed"]:
                    print("\n[+] Task completed!")
                    print("="*60)
                    print(f"Task ID:          {status_json.get('id')}")
                    print(f"Agent Name:       {status_json.get('agent_name')}")
                    print(f"Final Status:     {status}")
                    print(f"Progress Percent: {status_json.get('progress_percent')}%")
                    print("-" * 60)
                    print("Output Result:")
                    print(json.dumps(status_json.get("output_result", {}), indent=2))
                    if status_json.get("error_message"):
                        print(f"Error Message:    {status_json.get('error_message')}")
                    print("="*60)
                    
                    if status in ["success", "completed"]:
                        sys.exit(0)
                    else:
                        sys.exit(1)
                    
        except Exception as e:
            print(f"[-] Failed to get task status: {e}")
            if hasattr(e, 'read'):
                print(e.read().decode('utf-8'))
            sys.exit(1)
            
        time.sleep(2)

if __name__ == "__main__":
    main()
