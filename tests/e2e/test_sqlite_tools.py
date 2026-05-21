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
        
    traffic_file = os.path.join(data_dir, 'sqlite_traffic.json')
    prompt = "Create database 'testdb', list databases, perform 'CREATE TABLE test(id INT);', then delete 'testdb'"
    
    cmd = [agentcli_path, prompt, traffic_file]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"agentcli_replay failed with code {result.returncode}")
        print(result.stderr)
        sys.exit(1)
        
    output = result.stdout
    
    if "[Tool Result] Database 'testdb' created successfully." not in output:
        print("Failed to find create db result in output.")
        sys.exit(1)
        
    if "testdb" not in output or "Size" not in output:
        print("Failed to find list db result in output.")
        sys.exit(1)
        
    if "[Tool Result] Query executed successfully." not in output:
        print("Failed to find perform db result in output.")
        sys.exit(1)

    if "[Tool Result] Database 'testdb' deleted successfully." not in output:
        print("Failed to find delete db result in output.")
        sys.exit(1)
        
    print("Agent SQLite Database Tools test passed successfully.")

if __name__ == '__main__':
    run_test()
