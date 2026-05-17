import time
import os
from turbostar_runner import *

def test_file_dialog():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    testrun_dir = os.path.join(project_root, 'testrun')
    test_dir = "test_dialog_dir"
    test_file = "test_file.txt"
    test_subdir = "subdir"
    
    test_dir_abs = os.path.join(testrun_dir, test_dir)
    
    try:
        # Setup test directory and file in testrun/
        os.makedirs(os.path.join(test_dir_abs, test_subdir), exist_ok=True)
        with open(os.path.join(test_dir_abs, test_file), "w") as f:
            f.write("hello")

        runner.start()
        # 1. Open File Dialog
        runner.send_keys(KEY_ESC + 'f') # Alt-F
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
            runner.assert_in_log("Document loaded from")
            full_path = os.path.abspath(os.path.join(testrun_dir, test_dir, test_file))
            runner.assert_in_log(full_path)
        except AssertionError:
            print("Assertion failed. Log content:")
            print(log)
            raise
        
    finally:
        runner.cleanup()
        # Clean up test directory
        if os.path.exists(os.path.join(test_dir_abs, test_file)):
            os.remove(os.path.join(test_dir_abs, test_file))
        if os.path.exists(os.path.join(test_dir_abs, test_subdir)):
            os.rmdir(os.path.join(test_dir_abs, test_subdir))
        if os.path.exists(test_dir_abs):
            os.rmdir(test_dir_abs)

def test_file_dialog_autocomplete():
    runner = TurbostarRunner()
    project_root = os.environ.get('PROJECT_ROOT', os.getcwd())
    testrun_dir = os.path.join(project_root, 'testrun')
    test_dir = "test_dir_auto"
    test_file = "foobar.txt"
    
    test_dir_abs = os.path.join(testrun_dir, test_dir)
    
    try:
        os.makedirs(test_dir_abs, exist_ok=True)
        with open(os.path.join(test_dir_abs, test_file), "w") as f:
            f.write("hello foobar")

        runner.start()
        runner.send_keys(KEY_ESC + 'f') # Alt-F
        runner.send_keys('o') # Open
        time.sleep(0.5)

        runner.send_keys(test_dir + '\n')
        time.sleep(0.5)

        # Type 'foo' and hit enter, it should autocomplete to 'foobar.txt'
        runner.send_keys('foo\n')
        time.sleep(0.5)

        log = runner.get_log()
        try:
            full_path = os.path.abspath(os.path.join(testrun_dir, test_dir, test_file))
            runner.assert_in_log(full_path)
        except AssertionError:
            print("Assertion failed. Log content:")
            print(log)
            raise
        
    finally:
        runner.cleanup()
        if os.path.exists(os.path.join(test_dir_abs, test_file)):
            os.remove(os.path.join(test_dir_abs, test_file))
        if os.path.exists(test_dir_abs):
            os.rmdir(test_dir_abs)

if __name__ == "__main__":
    test_file_dialog()
    print("test_file_dialog passed!")
    test_file_dialog_autocomplete()
    print("test_file_dialog_autocomplete passed!")
