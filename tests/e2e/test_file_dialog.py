import time
import os
from turbostar_runner import TurbostarRunner

def test_file_dialog():
    runner = TurbostarRunner()
    test_dir = "test_dir"
    test_file = "test_file.txt"
    test_subdir = "subdir"
    
    try:
        # Setup test directory and file
        os.makedirs(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir, test_subdir), exist_ok=True)
        with open(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir, test_file), "w") as f:
            f.write("hello")

        runner.start()
        time.sleep(0.5)

        # 1. Open File Dialog
        runner.send_keys('\x1b' + 'f') # Alt-F
        runner.send_keys('o') # Open
        time.sleep(0.5)

        # 2. Type the directory name and enter it
        runner.send_keys(test_dir + '\n')
        time.sleep(0.5)

        # 3. Type the file name and confirm
        runner.send_keys(test_file + '\n')
        time.sleep(0.5)

        # 4. Check that the new window is opened
        log = runner.get_log()
        try:
            assert "Document loaded from" in log
            full_path = os.path.join(os.getcwd(), test_dir, test_file)
            assert full_path in log
        except AssertionError:
            print("Assertion failed. Log content:")
            print(log)
            raise
        
    finally:
        runner.cleanup()
        # Clean up test directory
        os.remove(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir, test_file))
        os.rmdir(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir, test_subdir))
        os.rmdir(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir))

def test_file_dialog_autocomplete():
    runner = TurbostarRunner()
    test_dir = "test_dir_auto"
    test_file = "foobar.txt"
    
    try:
        os.makedirs(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir), exist_ok=True)
        with open(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir, test_file), "w") as f:
            f.write("hello foobar")

        runner.start()
        time.sleep(0.5)

        runner.send_keys('\x1b' + 'f') # Alt-F
        runner.send_keys('o') # Open
        time.sleep(0.5)

        runner.send_keys(test_dir + '\n')
        time.sleep(0.5)

        # Type 'foo' and hit enter, it should autocomplete to 'foobar.txt'
        runner.send_keys('foo\n')
        time.sleep(0.5)

        log = runner.get_log()
        try:
            full_path = os.path.join(os.getcwd(), test_dir, test_file)
            assert full_path in log
        except AssertionError:
            print("Assertion failed. Log content:")
            print(log)
            raise
        
    finally:
        runner.cleanup()
        os.remove(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir, test_file))
        os.rmdir(os.path.join(os.environ.get('PROJECT_ROOT', os.getcwd()), test_dir))

if __name__ == "__main__":
    test_file_dialog()
    print("test_file_dialog passed!")
    test_file_dialog_autocomplete()
    print("test_file_dialog_autocomplete passed!")
