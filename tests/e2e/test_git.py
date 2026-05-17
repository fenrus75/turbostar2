import time
import os
import subprocess
from turbostar_runner import TurbostarRunner

def test_git_integration():
    runner = TurbostarRunner()
    # Create a unique temp home already handled by runner.start()
    
    try:
        # 1. Setup a temporary Git repository in a subfolder of testrun
        project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
        testrun_dir = os.path.join(project_root, 'testrun')
        repo_dir = os.path.join(testrun_dir, 'git_test_repo')
        os.makedirs(repo_dir, exist_ok=True)
        
        subprocess.run(['git', 'init'], cwd=repo_dir, capture_output=True)
        subprocess.run(['git', 'config', 'user.email', 'test@example.com'], cwd=repo_dir, capture_output=True)
        subprocess.run(['git', 'config', 'user.name', 'Test User'], cwd=repo_dir, capture_output=True)
        
        file_path = os.path.join(repo_dir, 'test.txt')
        with open(file_path, 'w') as f:
            f.write("Initial content\n")
            
        subprocess.run(['git', 'add', 'test.txt'], cwd=repo_dir, capture_output=True)
        subprocess.run(['git', 'commit', '-m', 'Initial commit'], cwd=repo_dir, capture_output=True)
        
        # 2. Start Turbostar with the file
        runner.start(filename="git_test_repo/test.txt")
        time.sleep(1.0)
        
        # 3. Verify [✔] (Clean) is shown
        runner.assert_text_on_screen("[✔]", timeout=5.0)

        # 4. Modify the file in the editor
        runner.send_keys("Modifying...")

        # 5. Save the file
        runner.send_ctrlk('s') # ^K S (Save)

        # 6. Verify [✎] (Dirty) is shown
        runner.assert_text_on_screen("[✎]", timeout=5.0)

        # 7. Use "Git add" via menu
        # Alt+G for Git menu, then 'a' for Add
        runner.send_keys('\x1b' + 'g')
        time.sleep(0.5)
        runner.send_keys('a')

        # Verify it staged the file
        # Wait a bit for the async command
        start_t = time.time()
        staged = False
        while time.time() - start_t < 3.0:
            res = subprocess.run(['git', 'status', '--porcelain', 'test.txt'], cwd=repo_dir, capture_output=True, text=True)
            if res.stdout.startswith('M '):
                staged = True
                break
            time.sleep(0.1)
        assert staged

        # 8. Create a new branch and verify it's shown in the UI
        subprocess.run(['git', 'checkout', '-b', 'feature-x'], cwd=repo_dir, capture_output=True)
        # Trigger refresh by saving
        runner.send_ctrlk('s')
        try:
            runner.assert_text_on_screen("(feature-x)", timeout=5.0)

        except AssertionError as e:
            print(f"Log contents:\n{runner.get_log()}")
            raise e

        runner.send_ctrlk('q') # Ctrl-C

        runner.wait(timeout=5)
        
    finally:
        runner.cleanup()
        import shutil
        if os.path.exists(repo_dir):
            shutil.rmtree(repo_dir)

if __name__ == "__main__":
    test_git_integration()
    print("test_git_integration passed!")
