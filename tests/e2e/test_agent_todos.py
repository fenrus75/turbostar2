import subprocess
import sys
import os

def run_test():
    build_dir = os.environ.get('MESON_BUILD_ROOT', 'build')
    data_dir = os.path.join(os.environ.get('PROJECT_ROOT', '.'), 'tests', 'data')
    
    agentcli_path = os.path.join(build_dir, 'agentcli_replay')
    if not os.path.exists(agentcli_path):
        print(f"Error: {agentcli_path} not found.")
        sys.exit(1)
        
    traffic_file = os.path.join(data_dir, 'todo_traffic.json')
    prompt = "Add a task to read the Readme, then list my tasks. Then mark the first one complete using the tools available."
    
    cmd = [agentcli_path, prompt, traffic_file]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"agentcli_replay failed with code {result.returncode}")
        print(result.stderr)
        sys.exit(1)
        
    output = result.stdout
    
    if "[Tool Result] Added todo: Read the Readme file" not in output:
        print("Failed to find add todo result in output.")
        sys.exit(1)
        
    if "[Tool Result] - [ ] Read the Readme file" not in output:
        print("Failed to find list todos result in output.")
        sys.exit(1)
        
    if "[Tool Result] Task marked complete." not in output:
        print("Failed to find complete todo result in output.")
        sys.exit(1)
        
    print("Agent To-Do Management Tools test passed successfully.")

if __name__ == '__main__':
    run_test()
