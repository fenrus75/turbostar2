import time
from turbostar_runner import *

def test_exit_save_prompt_loop():
    runner = TurbostarRunner()
    try:
        # Start with two files
        runner.start(filename="file1.txt file2.txt")
        
        # We start focused on file2.txt. Let's make it dirty.
        runner.send_keys("Dirty edits in file2")
        runner.assert_text_on_screen("file2.txt*")
        
        # Switch focus to file1.txt (which is clean)
        runner.send_keys(KEY_ESC + '1')
        runner.assert_text_on_screen("file1.txt")
        runner.assert_text_not_on_screen("file1.txt*")
        
        # Trigger quit
        runner.send_ctrlk('q')
        
        # It should prompt to save file2.txt
        runner.assert_text_on_screen("Save changes to file2.txt?", timeout=2.0)
        
        # Choose Discard (Alt+D)
        runner.send_keys('\x1b' + 'd')
        
        # The editor should now exit. We wait for it to exit.
        runner.wait(timeout=5.0)
        
    finally:
        runner.cleanup()

if __name__ == "__main__":
    test_exit_save_prompt_loop()
    print("test_exit_save_prompt_loop passed!")
