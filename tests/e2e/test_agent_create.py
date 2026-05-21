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
        
    traffic_file = os.path.join(data_dir, 'agent_create_traffic.json')
    prompt = "Create a subagent to check if test.c can be optimized for performance"
    
    cmd = [agentcli_path, prompt, traffic_file]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"agentcli_replay failed with code {result.returncode}")
        print(result.stderr)
        sys.exit(1)
        
    output = result.stdout
    
    if "[Tool Result] Agent " not in output or "created successfully with ID:" not in output:
        print("Failed to find agent creation result in output.")
        print(output)
        sys.exit(1)
        
    print("Agent Create Tool test passed successfully.")

if __name__ == '__main__':
    run_test()
