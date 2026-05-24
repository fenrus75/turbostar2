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
        
    traffic_file = os.path.join(data_dir, 'test_management_traffic.json')
    prompt = "List the tests and run the event logger test."
    
    cmd = [agentcli_path, prompt, traffic_file]
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"agentcli_replay failed with code {result.returncode}")
        print(result.stderr)
        sys.exit(1)
        
    output = result.stdout
    
    if "[Tool Result] Available Tests:" not in output:
        print("Failed to find list tests result in output.")
        print(output)
        sys.exit(1)
        
    if "unit_event_logger" not in output:
        print("Failed to find expected test name in output.")
        sys.exit(1)
        
    if "[Tool Result] ```bash\n$ MESON_TESTTHREADS=2 meson test -C" not in output:
        print("Failed to find run tests result in output.")
        print(output)
        sys.exit(1)
        
    if "I have listed the tests and ran the event logger test successfully." not in output:
        print("Failed to find assistant final response in output.")
        sys.exit(1)
        
    print("Test Management Tools replay test passed successfully.")

if __name__ == '__main__':
    run_test()
