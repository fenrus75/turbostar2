import subprocess
import sys
import os

def run_test():
    build_dir = os.environ.get('MESON_BUILD_ROOT', 'build')
    data_dir = os.path.join(os.environ.get('PROJECT_ROOT', '.'), 'tests', 'data')
    
    agentcli_path = os.path.join(build_dir, 'agentcli_replay')
    if not os.path.exists(agentcli_path):
        # Fallback to local build path if we are running manually from parent dir
        agentcli_path = './build/agentcli_replay'
        if not os.path.exists(agentcli_path):
            print(f"Error: agentcli_replay not found.")
            sys.exit(1)
        
    traffic_file = os.path.join(data_dir, 'elf_tools_traffic.json')
    prompt = "Activate the x86 tools. Inspect the ELF file at build/turbostar. List its sections and search for symbols containing 'dump'."
    
    cmd = [agentcli_path, prompt, traffic_file]
    
    project_root = os.environ.get('PROJECT_ROOT', '.')
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=project_root)
    
    if result.returncode != 0:
        print(f"agentcli_replay failed with code {result.returncode}")
        print(result.stderr)
        sys.exit(1)
        
    output = result.stdout
    
    if "[Tool Result] Tool family 'x86' has been successfully activated." not in output:
        print("Failed to find tool family x86 activation result in output.")
        sys.exit(1)
        
    if "[Tool Result] ### ELF Section Headers:" not in output:
        print("Failed to find ELF Section Headers tool result in output.")
        sys.exit(1)
        
    if "[Tool Result] ### ELF Symbol Table: build/turbostar" not in output:
        print("Failed to find ELF Symbol Table tool result in output.")
        sys.exit(1)
        
    print("Agent ELF Tools Replay E2E test passed successfully.")

if __name__ == '__main__':
    run_test()
