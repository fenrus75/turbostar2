#!/usr/bin/env python3
import urllib.request
import urllib.parse
import json
import time
import os
import sys

BASE_URL = "http://localhost:7820/a2a/v1"
AGENT_NAME = "research"

def main():
    instructions = "Can you return the current time?"
    if len(sys.argv) > 1:
        instructions = " ".join(sys.argv[1:])

    print(f"[*] Dispatching task to '{AGENT_NAME}' agent...")
    print(f"[*] Prompt: '{instructions}'")
    
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
                    output_res = status_json.get("output_result", {})
                    
                    print("\n[+] Task Execution Complete!")
                    print("="*65)
                    print(f" Task ID:          {status_json.get('id')}")
                    print(f" Target Agent:     {status_json.get('agent_name')}")
                    print(f" Final Status:     {status}")
                    print(f" Progress:         {status_json.get('progress_percent')}%")
                    
                    if isinstance(output_res, dict):
                        summary = output_res.get("summary", "N/A")
                        project_dir = output_res.get("project_dir", f"/tmp/turbostar_a2a_{task_id}")
                        agent_response = output_res.get("response") or output_res.get("output") or output_res.get("result")
                        
                        print(f" Project Dir:      {project_dir}")
                        print(f" Summary:          {summary}")
                        
                        if agent_response:
                            print("-" * 65)
                            print(" >>> AGENT RETURN TEXT / RESPONSE <<<")
                            print(agent_response)
                    
                    print("-" * 65)
                    print(" Full Output Payload:")
                    print(json.dumps(output_res, indent=2))
                    
                    # Check for session log
                    task_workspace = f"/tmp/turbostar_a2a_{task_id}"
                    log_file = os.path.join(task_workspace, "session.log")
                    if os.path.exists(log_file):
                        print("-" * 65)
                        print(f" Session Log Output ({log_file}):")
                        try:
                            with open(log_file, "r") as f:
                                log_content = f.read().strip()
                                print(log_content if log_content else "(empty log)")
                        except Exception as log_err:
                            print(f"(Could not read session log: {log_err})")

                    if status_json.get("error_message"):
                        print("-" * 65)
                        print(f" Error Message:    {status_json.get('error_message')}")
                    print("="*65)
                    
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
